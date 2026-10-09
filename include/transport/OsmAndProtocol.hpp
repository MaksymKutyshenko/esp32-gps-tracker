#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "logic/Fix.hpp"
#include "logic/TrackRecord.hpp"

// Position report in the OsmAnd protocol that Traccar (and most self-hosted
// trackers) accept on port 5055: one HTTP GET per point with the values in
// the query string. Chosen because it needs no JSON, no TLS and no body —
// the SIM800C's HTTP stack handles it with three AT commands.
//
//   http://host:5055/?id=<device>&lat=..&lon=..&timestamp=<unix>&speed=<kn>
//        &bearing=..&altitude=<m>&hdop=..&batt=<%>
//
// Pure logic, no ESP-IDF dependency; unit-tested on the host.
namespace OsmAndProtocol
{
    // Days since 1970-01-01 for a proleptic Gregorian date (Howard
    // Hinnant's days_from_civil), valid for the years the receiver reports.
    inline int64_t daysFromCivil(int32_t year, uint32_t month, uint32_t day)
    {
        year -= month <= 2;
        const int32_t era = (year >= 0 ? year : year - 399) / 400;
        const uint32_t yearOfEra = static_cast<uint32_t>(year - era * 400);
        const uint32_t dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
        const uint32_t dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
        return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(dayOfEra) - 719468;
    }

    inline int64_t unixTime(const UtcTime &utc)
    {
        return daysFromCivil(utc.year, utc.month, utc.day) * 86400 +
               static_cast<int64_t>(utc.hour) * 3600 +
               static_cast<int64_t>(utc.minute) * 60 + utc.second;
    }

    // Li-ion cell: 3.3 V empty, 4.2 V full, linear in between. Good enough
    // for a status figure; the display shows the voltage itself.
    inline uint8_t batteryPercent(uint16_t millivolts)
    {
        if (millivolts <= 3300)
        {
            return 0;
        }
        if (millivolts >= 4200)
        {
            return 100;
        }
        return static_cast<uint8_t>((millivolts - 3300) * 100 / 900);
    }

    // Returns the URL length, or 0 if it did not fit.
    inline size_t buildUrl(const char *serverUrl, const char *deviceId,
                           const TrackRecord &record, char *out, size_t capacity)
    {
        const Fix &f = record.fix;
        char lat[16], lon[16], alt[16], hdop[12], course[12], speed[12];
        TrackRecordFormat::formatFixed(lat, sizeof lat, f.latitudeE7, 7);
        TrackRecordFormat::formatFixed(lon, sizeof lon, f.longitudeE7, 7);
        TrackRecordFormat::formatFixed(alt, sizeof alt, f.altitudeCm, 2);
        TrackRecordFormat::formatFixed(hdop, sizeof hdop, f.hdopCenti, 2);
        TrackRecordFormat::formatFixed(course, sizeof course, f.courseDeciDeg, 1);
        // cm/s -> centi-knots: 1 kn = 51.44 cm/s, so * 100 / 51.44 = * 10000 / 5144.
        TrackRecordFormat::formatFixed(speed, sizeof speed,
                                       static_cast<int64_t>(f.speedCmPerS) * 10'000 / 5144, 2);

        const int n = snprintf(out, capacity,
                               "%s/?id=%s&lat=%s&lon=%s&timestamp=%lld&speed=%s&bearing=%s"
                               "&altitude=%s&hdop=%s&batt=%u",
                               serverUrl, deviceId, lat, lon,
                               static_cast<long long>(unixTime(f.utc)), speed, course,
                               alt, hdop, batteryPercent(record.batteryMv));
        return (n > 0 && static_cast<size_t>(n) < capacity) ? static_cast<size_t>(n) : 0;
    }
}
