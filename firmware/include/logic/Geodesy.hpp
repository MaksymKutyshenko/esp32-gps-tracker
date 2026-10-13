#pragma once

#include <math.h>
#include <stdint.h>

// Distance between two fixes on the WGS-84 sphere (haversine). Double is
// used here deliberately: the difference of two angles near 1e-7 degree
// resolution does not survive single precision, and this runs once per
// logged fix, not per sample.
//
// Pure logic, no ESP-IDF dependency; unit-tested on the host.
namespace Geodesy
{
    constexpr double earthRadiusM = 6'371'000.0;
    constexpr double degreesPerE7 = 1e-7;

    inline double toRadians(int32_t e7)
    {
        return static_cast<double>(e7) * degreesPerE7 * M_PI / 180.0;
    }

    // Great-circle distance in metres, rounded to the nearest metre.
    inline uint32_t distanceMeters(int32_t lat1E7, int32_t lon1E7,
                                   int32_t lat2E7, int32_t lon2E7)
    {
        const double lat1 = toRadians(lat1E7);
        const double lat2 = toRadians(lat2E7);
        const double dLat = lat2 - lat1;
        const double dLon = toRadians(lon2E7) - toRadians(lon1E7);

        const double a = sin(dLat / 2) * sin(dLat / 2) +
                         cos(lat1) * cos(lat2) * sin(dLon / 2) * sin(dLon / 2);
        const double c = 2 * atan2(sqrt(a), sqrt(1 - a));
        return static_cast<uint32_t>(earthRadiusM * c + 0.5);
    }
}
