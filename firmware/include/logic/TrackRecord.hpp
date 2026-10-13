#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "logic/Fix.hpp"

// One line of the track log: a fix plus what the tracker knew about itself
// at that moment. Built in exactly one place (TrackRecord::from) so the
// CSV formatter and the uplink payload describe the same data.
//
// Pure logic, no ESP-IDF dependency; unit-tested on the host.
struct TrackRecord
{
    Fix fix;
    uint16_t batteryMv;

    static TrackRecord from(const Fix &fix, uint16_t batteryMv)
    {
        TrackRecord record = {};
        record.fix = fix;
        record.batteryMv = batteryMv;
        return record;
    }
};

namespace TrackRecordFormat
{
    constexpr const char *csvHeader =
        "utc,lat,lon,alt_m,speed_kmh,course_deg,sats,hdop,batt_mv\r\n";

    // Writes a fixed-point value as "-12.3456789" without printf's float
    // formatting; `scale` is the number of fraction digits.
    inline size_t formatFixed(char *out, size_t capacity, int64_t value, uint8_t scale)
    {
        int64_t divisor = 1;
        for (uint8_t i = 0; i < scale; ++i)
        {
            divisor *= 10;
        }
        const char *sign = value < 0 ? "-" : "";
        const int64_t magnitude = value < 0 ? -value : value;
        int written = 0;
        if (scale == 0)
        {
            written = snprintf(out, capacity, "%s%lld", sign,
                               static_cast<long long>(magnitude));
        }
        else
        {
            written = snprintf(out, capacity, "%s%lld.%0*lld", sign,
                               static_cast<long long>(magnitude / divisor),
                               static_cast<int>(scale),
                               static_cast<long long>(magnitude % divisor));
        }
        return written > 0 && static_cast<size_t>(written) < capacity
                   ? static_cast<size_t>(written)
                   : 0;
    }

    // One CSV line with CR LF; returns the length, or 0 if it did not fit.
    inline size_t csvLine(const TrackRecord &record, char *out, size_t capacity)
    {
        const Fix &f = record.fix;
        size_t length = 0;

        auto append = [&](size_t n) -> bool
        {
            if (n == 0)
            {
                length = 0;
                return false;
            }
            length += n;
            return true;
        };
        auto literal = [&](const char *text) -> bool
        {
            int n = snprintf(out + length, capacity - length, "%s", text);
            return append(n > 0 && static_cast<size_t>(n) < capacity - length
                              ? static_cast<size_t>(n) : 0);
        };

        int n = snprintf(out, capacity, "%04u-%02u-%02uT%02u:%02u:%02uZ,",
                         f.utc.year, f.utc.month, f.utc.day,
                         f.utc.hour, f.utc.minute, f.utc.second);
        if (!append(n > 0 && static_cast<size_t>(n) < capacity ? static_cast<size_t>(n) : 0))
        {
            return 0;
        }

        if (!append(formatFixed(out + length, capacity - length, f.latitudeE7, 7)) || !literal(",") ||
            !append(formatFixed(out + length, capacity - length, f.longitudeE7, 7)) || !literal(",") ||
            !append(formatFixed(out + length, capacity - length, f.altitudeCm, 2)) || !literal(","))
        {
            return 0;
        }

        // 1 cm/s = 0.036 km/h = 0.36 units of 0.1 km/h, hence * 36 / 100.
        const int64_t speedDeciKmh = static_cast<int64_t>(f.speedCmPerS) * 36 / 100;
        if (!append(formatFixed(out + length, capacity - length, speedDeciKmh, 1)) || !literal(",") ||
            !append(formatFixed(out + length, capacity - length, f.courseDeciDeg, 1)) || !literal(","))
        {
            return 0;
        }

        n = snprintf(out + length, capacity - length, "%u,", f.satellites);
        if (!append(n > 0 && static_cast<size_t>(n) < capacity - length ? static_cast<size_t>(n) : 0))
        {
            return 0;
        }
        if (!append(formatFixed(out + length, capacity - length, f.hdopCenti, 2)) || !literal(","))
        {
            return 0;
        }
        n = snprintf(out + length, capacity - length, "%u\r\n", record.batteryMv);
        if (!append(n > 0 && static_cast<size_t>(n) < capacity - length ? static_cast<size_t>(n) : 0))
        {
            return 0;
        }
        return length;
    }
}
