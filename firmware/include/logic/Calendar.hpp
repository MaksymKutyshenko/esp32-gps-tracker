#pragma once

#include <stdint.h>

#include "logic/Fix.hpp"

// Date arithmetic on the receiver's UTC time, shared by the upload
// protocol (Unix timestamps) and the display (local time).
//
// Pure logic, no ESP-IDF dependency; unit-tested on the host.
namespace Calendar
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

    // The inverse (Hinnant's civil_from_days).
    inline void civilFromDays(int64_t days, uint16_t &year, uint8_t &month, uint8_t &day)
    {
        days += 719468;
        const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
        const uint32_t dayOfEra = static_cast<uint32_t>(days - era * 146097);
        const uint32_t yearOfEra =
            (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
        const uint32_t dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
        const uint32_t mp = (5 * dayOfYear + 2) / 153;
        day = static_cast<uint8_t>(dayOfYear - (153 * mp + 2) / 5 + 1);
        month = static_cast<uint8_t>(mp < 10 ? mp + 3 : mp - 9);
        year = static_cast<uint16_t>(yearOfEra + era * 400 + (month <= 2));
    }

    inline int64_t unixTime(const UtcTime &utc)
    {
        return daysFromCivil(utc.year, utc.month, utc.day) * 86400 +
               static_cast<int64_t>(utc.hour) * 3600 +
               static_cast<int64_t>(utc.minute) * 60 + utc.second;
    }

    inline UtcTime fromUnixTime(int64_t seconds)
    {
        UtcTime t = {};
        const int64_t days = seconds >= 0 ? seconds / 86400 : (seconds - 86399) / 86400;
        civilFromDays(days, t.year, t.month, t.day);
        const uint32_t ofDay = static_cast<uint32_t>(seconds - days * 86400);
        t.hour = static_cast<uint8_t>(ofDay / 3600);
        t.minute = static_cast<uint8_t>((ofDay / 60) % 60);
        t.second = static_cast<uint8_t>(ofDay % 60);
        return t;
    }

    // Day of the last Sunday of a month.
    inline uint8_t lastSunday(uint16_t year, uint8_t month)
    {
        const uint8_t lastDay = month == 12 ? 31 : static_cast<uint8_t>(
            daysFromCivil(year, month + 1, 1) - daysFromCivil(year, month, 1));
        // 1970-01-01 was a Thursday: (days + 4) % 7 gives 0 = Sunday.
        const int64_t weekday = (daysFromCivil(year, month, lastDay) + 4) % 7;
        return static_cast<uint8_t>(lastDay - weekday);
    }
}

// Local time for the display. Records and uploads stay in UTC; only what
// the operator reads on the screen is shifted. The European rule is the
// one Ukraine follows: summer time from 01:00 UTC on the last Sunday of
// March to 01:00 UTC on the last Sunday of October, one hour ahead of the
// standard offset. With the rule disabled the standard offset applies all
// year.
class TimeZone
{
public:
    struct Params
    {
        int32_t standardOffsetMinutes; // e.g. 120 for Kyiv (UTC+2)
        bool europeanSummerTime;
    };

    explicit TimeZone(const Params &params) : params_(params) {}

    int32_t offsetMinutes(const UtcTime &utc) const
    {
        if (!params_.europeanSummerTime)
        {
            return params_.standardOffsetMinutes;
        }
        const int64_t now = Calendar::unixTime(utc);
        const int64_t start = Calendar::unixTime({utc.year, 3, Calendar::lastSunday(utc.year, 3), 1, 0, 0});
        const int64_t end = Calendar::unixTime({utc.year, 10, Calendar::lastSunday(utc.year, 10), 1, 0, 0});
        const bool summer = now >= start && now < end;
        return params_.standardOffsetMinutes + (summer ? 60 : 0);
    }

    UtcTime toLocal(const UtcTime &utc) const
    {
        return Calendar::fromUnixTime(Calendar::unixTime(utc) + static_cast<int64_t>(offsetMinutes(utc)) * 60);
    }

private:
    Params params_;
};
