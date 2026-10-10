#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "logic/DecimalParser.hpp"
#include "logic/Fix.hpp"

// Validates NMEA 0183 sentences and folds RMC and GGA into a Fix.
//
// Validation is strict on purpose: a sentence is accepted only with a
// correct checksum and a plausible field count, and any field that fails to
// parse rejects the whole sentence. A rejected sentence leaves the Fix
// untouched, so the caller can never publish a half-parsed position.
//
// Talker ids are ignored: $GPRMC, $GNRMC, $GLRMC... all carry the same
// payload. Epoch boundary: RMC is the sentence that carries the validity
// flag and the date, so the caller publishes the Fix after each RMC.
//
// Pure logic, no ESP-IDF dependency; header-only so the host tests can
// include it directly.
class NmeaParser
{
public:
    enum class Sentence : uint8_t
    {
        Rmc,       // position epoch complete, fix() is up to date
        Gga,       // quality / satellites / altitude merged into fix()
        Gsv,       // satellites in view noted; folded into fix() at the next RMC
        Other,     // valid sentence of a type this parser does not use
        Invalid,   // checksum, framing or field failure; counted
    };

    static constexpr size_t maxSentenceLength = 82;

    // `line` is one sentence from '$' to before CR/LF, NUL-terminated.
    Sentence parse(const char *line)
    {
        if (!verifyChecksum(line))
        {
            ++checksumFailures_;
            return Sentence::Invalid;
        }

        char fields[maxSentenceLength + 1];
        const char *field[maxFields] = {};
        size_t count = splitFields(line, fields, field);
        if (count < 2)
        {
            ++fieldFailures_;
            return Sentence::Invalid;
        }

        // field[0] is "$GNRMC": the type is its last three characters.
        const char *type = field[0] + strlen(field[0]) - 3;
        if (strcmp(type, "RMC") == 0)
        {
            if (!parseRmc(field, count))
            {
                ++fieldFailures_;
                return Sentence::Invalid;
            }
            return Sentence::Rmc;
        }
        if (strcmp(type, "GGA") == 0)
        {
            if (!parseGga(field, count))
            {
                ++fieldFailures_;
                return Sentence::Invalid;
            }
            return Sentence::Gga;
        }
        if (strcmp(type, "GSV") == 0)
        {
            if (!parseGsv(field, count))
            {
                ++fieldFailures_;
                return Sentence::Invalid;
            }
            return Sentence::Gsv;
        }
        return Sentence::Other;
    }

    const Fix &fix() const { return fix_; }

    uint32_t checksumFailures() const { return checksumFailures_; }
    uint32_t fieldFailures() const { return fieldFailures_; }

    // "$<payload>*HH": HH is the XOR of every payload byte, in upper-case
    // hex. A sentence without '*' is rejected; some receivers can be set to
    // omit the checksum, but then nothing protects the position data.
    static bool verifyChecksum(const char *line)
    {
        if (line == nullptr || line[0] != '$')
        {
            return false;
        }
        uint8_t computed = 0;
        const char *p = line + 1;
        for (; *p != '\0' && *p != '*'; ++p)
        {
            computed ^= static_cast<uint8_t>(*p);
        }
        if (*p != '*' || p - line > static_cast<ptrdiff_t>(maxSentenceLength))
        {
            return false;
        }
        int32_t high = hexValue(p[1]);
        int32_t low = hexValue(p[2]);
        if (high < 0 || low < 0 || p[3] != '\0')
        {
            return false;
        }
        return computed == static_cast<uint8_t>(high * 16 + low);
    }

private:
    // GSV in NMEA 4.11 is the widest sentence: 4 header fields, four
    // satellites of 4 fields each and a trailing signal id = 21.
    static constexpr size_t maxFields = 24;

    static int32_t hexValue(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    }

    // Copies the payload into `scratch`, replaces every ',' by NUL and
    // points field[i] at each piece. The '*' ends the last field.
    static size_t splitFields(const char *line, char *scratch, const char **field)
    {
        size_t count = 0;
        size_t i = 0;
        field[count++] = scratch;
        for (; line[i] != '\0' && line[i] != '*' && i < maxSentenceLength; ++i)
        {
            if (line[i] == ',')
            {
                scratch[i] = '\0';
                if (count >= maxFields)
                {
                    return 0;
                }
                field[count++] = scratch + i + 1;
            }
            else
            {
                scratch[i] = line[i];
            }
        }
        scratch[i] = '\0';
        return count;
    }

    // "ddmm.mmmmm" / "dddmm.mmmmm" with hemisphere -> degrees * 1e7.
    static bool parseCoordinate(const char *text, const char *hemisphere,
                                char positive, char negative, int32_t &outE7)
    {
        int64_t scaled = 0; // ddmm.mmmmm * 1e5
        if (!DecimalParser::parseScaled(text, 5, scaled) || scaled < 0)
        {
            return false;
        }
        const int64_t minutesScaled = scaled % 10'000'000;  // mm.mmmmm * 1e5
        const int64_t degrees = scaled / 10'000'000;
        if (minutesScaled >= 6'000'000)
        {
            return false; // more than 60 minutes
        }
        // minutes * 1e5 -> degrees * 1e7: multiply by 1e7 / (60 * 1e5) = 5/3.
        int64_t e7 = degrees * 10'000'000 + (minutesScaled * 5 + 1) / 3;

        if (hemisphere[0] == negative && hemisphere[1] == '\0')
        {
            e7 = -e7;
        }
        else if (!(hemisphere[0] == positive && hemisphere[1] == '\0'))
        {
            return false;
        }
        if (e7 < -1'800'000'000 || e7 > 1'800'000'000)
        {
            return false;
        }
        outE7 = static_cast<int32_t>(e7);
        return true;
    }

    static bool parseTime(const char *text, UtcTime &utc)
    {
        uint32_t hhmmss = 0;
        if (!DecimalParser::parseDigits(text, 6, hhmmss))
        {
            return false;
        }
        const uint32_t hour = hhmmss / 10'000;
        const uint32_t minute = (hhmmss / 100) % 100;
        const uint32_t second = hhmmss % 100;
        if (hour > 23 || minute > 59 || second > 59)
        {
            return false;
        }
        utc.hour = static_cast<uint8_t>(hour);
        utc.minute = static_cast<uint8_t>(minute);
        utc.second = static_cast<uint8_t>(second);
        return true;
    }

    static bool parseDate(const char *text, UtcTime &utc)
    {
        uint32_t ddmmyy = 0;
        if (!DecimalParser::parseDigits(text, 6, ddmmyy) || text[6] != '\0')
        {
            return false;
        }
        const uint32_t day = ddmmyy / 10'000;
        const uint32_t month = (ddmmyy / 100) % 100;
        const uint32_t year = ddmmyy % 100;
        if (day < 1 || day > 31 || month < 1 || month > 12)
        {
            return false;
        }
        utc.day = static_cast<uint8_t>(day);
        utc.month = static_cast<uint8_t>(month);
        utc.year = static_cast<uint16_t>(2000 + year);
        return true;
    }

    // $--RMC,hhmmss.ss,A,ddmm.mm,N,dddmm.mm,E,speed_kn,course,ddmmyy,...
    bool parseRmc(const char **field, size_t count)
    {
        if (count < 10)
        {
            return false;
        }
        Fix next = fix_;
        next.valid = (field[2][0] == 'A' && field[2][1] == '\0');

        // Until the receiver has decoded time from at least one satellite
        // it sends RMC with empty time and date fields; that is a legal
        // "no time yet" sentence, not a malformed one. With a valid fix
        // both fields are mandatory.
        const bool haveTime = field[1][0] != '\0';
        const bool haveDate = field[9][0] != '\0';
        if (next.valid && (!haveTime || !haveDate))
        {
            return false;
        }
        if (haveTime && !parseTime(field[1], next.utc))
        {
            return false;
        }
        if (haveDate && !parseDate(field[9], next.utc))
        {
            return false;
        }

        if (next.valid)
        {
            if (!parseCoordinate(field[3], field[4], 'N', 'S', next.latitudeE7) ||
                !parseCoordinate(field[5], field[6], 'E', 'W', next.longitudeE7))
            {
                return false;
            }
            if (next.latitudeE7 < -900'000'000 || next.latitudeE7 > 900'000'000)
            {
                return false;
            }

            // Speed and course may be empty while stationary on some
            // receivers; an empty field means zero, a malformed one is an
            // error.
            int64_t milliKnots = 0;
            if (field[7][0] != '\0' &&
                (!DecimalParser::parseScaled(field[7], 3, milliKnots) || milliKnots < 0))
            {
                return false;
            }
            // 1 knot = 51.4444 cm/s, so 1 milli-knot = 51444 / 1e6 cm/s.
            next.speedCmPerS = static_cast<uint32_t>(milliKnots * 51'444 / 1'000'000);

            int64_t deciDegrees = 0;
            if (field[8][0] != '\0' &&
                (!DecimalParser::parseScaled(field[8], 1, deciDegrees) ||
                 deciDegrees < 0 || deciDegrees >= 3600))
            {
                return false;
            }
            next.courseDeciDeg = static_cast<uint16_t>(deciDegrees);
        }

        // GSV counts arrive per constellation between two RMCs; the epoch
        // takes the largest one, which is "satellites heard" for the user.
        next.satellitesInView = inViewPending_;
        next.satellitesTracked = trackedPending_;
        next.maxSnrDbHz = maxSnrPending_;
        inViewPending_ = 0;
        trackedPending_ = 0;
        maxSnrPending_ = 0;

        fix_ = next;
        return true;
    }

    // $--GSV,total_msgs,msg_num,sats_in_view,{prn,elev,azim,cno}*4[,sigId]
    // Field 3 is the talker's count of satellites above the horizon; each
    // group's C/N0 is empty when that satellite is not actually received.
    // "In view" tells whether the almanac is loaded; "tracked" and the
    // strongest C/N0 tell whether the antenna delivers a signal at all.
    bool parseGsv(const char **field, size_t count)
    {
        if (count < 4)
        {
            return false;
        }
        int64_t value = 0;
        if (field[3][0] == '\0')
        {
            return true; // some receivers send an empty count with no satellites
        }
        if (!DecimalParser::parseScaled(field[3], 0, value) || value < 0 || value > 99)
        {
            return false;
        }
        if (value > inViewPending_)
        {
            inViewPending_ = static_cast<uint8_t>(value);
        }
        for (size_t i = 4; i + 3 < count; i += 4)
        {
            const char *cno = field[i + 3];
            if (cno[0] == '\0')
            {
                continue;
            }
            if (!DecimalParser::parseScaled(cno, 0, value) || value < 0 || value > 99)
            {
                return false;
            }
            if (trackedPending_ < 255)
            {
                ++trackedPending_;
            }
            if (value > maxSnrPending_)
            {
                maxSnrPending_ = static_cast<uint8_t>(value);
            }
        }
        return true;
    }

    // $--GGA,hhmmss.ss,ddmm.mm,N,dddmm.mm,E,quality,sats,hdop,alt,M,...
    bool parseGga(const char **field, size_t count)
    {
        if (count < 10)
        {
            return false;
        }
        Fix next = fix_;

        int64_t value = 0;
        if (!DecimalParser::parseScaled(field[6], 0, value) || value < 0 || value > 9)
        {
            return false;
        }
        next.quality = static_cast<uint8_t>(value);

        if (!DecimalParser::parseScaled(field[7], 0, value) || value < 0 || value > 99)
        {
            return false;
        }
        next.satellites = static_cast<uint8_t>(value);

        // HDOP and altitude are empty without a fix.
        if (field[8][0] != '\0')
        {
            if (!DecimalParser::parseScaled(field[8], 2, value) || value < 0 || value > 65'535)
            {
                return false;
            }
            next.hdopCenti = static_cast<uint16_t>(value);
        }
        if (field[9][0] != '\0')
        {
            if (!DecimalParser::parseScaled(field[9], 2, value) ||
                value < -100'000'000 || value > 100'000'000)
            {
                return false;
            }
            next.altitudeCm = static_cast<int32_t>(value);
        }

        fix_ = next;
        return true;
    }

    Fix fix_ = {};
    uint8_t inViewPending_ = 0;
    uint8_t trackedPending_ = 0;
    uint8_t maxSnrPending_ = 0;
    uint32_t checksumFailures_ = 0;
    uint32_t fieldFailures_ = 0;
};
