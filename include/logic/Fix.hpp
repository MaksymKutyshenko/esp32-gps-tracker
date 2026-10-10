#pragma once

#include <stdint.h>

// One GNSS position epoch, assembled from the RMC and GGA sentences of the
// same second. Everything is integer fixed-point so the whole pipeline runs
// without floating point and the record is trivially copyable through a
// FreeRTOS queue.
//
// Pure data, no ESP-IDF dependency: used by the host tests.
struct UtcTime
{
    uint16_t year;   // full year, 2000..2099 from the two-digit NMEA date
    uint8_t month;   // 1..12
    uint8_t day;     // 1..31
    uint8_t hour;    // 0..23
    uint8_t minute;  // 0..59
    uint8_t second;  // 0..59
};

struct Fix
{
    // From RMC: 'A' status. False means the position fields are stale or
    // unknown and must not be logged or uploaded.
    bool valid;

    int32_t latitudeE7;      // degrees * 1e7, north positive (1 unit ~ 1.1 cm)
    int32_t longitudeE7;     // degrees * 1e7, east positive
    int32_t altitudeCm;      // metres above mean sea level * 100 (GGA)
    uint32_t speedCmPerS;    // ground speed (RMC knots converted)
    uint16_t courseDeciDeg;  // track over ground * 10, 0..3599 (RMC)
    uint16_t hdopCenti;      // horizontal dilution * 100 (GGA)
    uint8_t satellites;      // satellites used in the solution (GGA)
    uint8_t satellitesInView; // satellites the receiver can hear (GSV), max over talkers per epoch
    uint8_t quality;         // GGA fix quality: 0 none, 1 GPS, 2 DGPS, ...
    UtcTime utc;
};
