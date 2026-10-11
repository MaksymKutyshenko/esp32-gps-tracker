#pragma once

#include <stdint.h>

#include "logic/Fix.hpp"
#include "logic/Geodesy.hpp"

// Decides whether a fix is worth a track record. This is the feedback loop
// of the tracker: measured movement sets how often the device records.
//
//   moving      -> every movingIntervalMs, or sooner once the position has
//                  moved minDistanceM from the last record
//   stationary  -> every stationaryIntervalMs (heartbeat, proves the device
//                  is alive without filling the card with one point)
//
// A fix also has to be worth recording: with few satellites or a poor
// geometry (HDOP) the position wanders tens of metres while the device
// sits still, and every wander would be logged and uploaded as movement.
// Such fixes are skipped; 0 disables either gate.
//
// The parameters come from Settings (NVS), so the trade-off between track
// detail and storage/airtime is a field setting, not a rebuild. Pure
// logic, no ESP-IDF dependency; unit-tested on the host.
class LogPolicy
{
public:
    struct Params
    {
        uint32_t movingIntervalMs;
        uint32_t stationaryIntervalMs;
        uint32_t minDistanceM;
        uint8_t minSatellites;   // used in the solution; 0 = any
        uint16_t maxHdopCenti;   // HDOP * 100; 0 = any
    };

    explicit LogPolicy(const Params &params) : params_(params) {}

    void setParams(const Params &params) { params_ = params; }
    const Params &params() const { return params_; }

    bool shouldLog(const Fix &fix, uint32_t nowMs, bool moving) const
    {
        if (!fix.valid || !isAccurate(fix))
        {
            return false;
        }
        if (!hasLast_)
        {
            return true;
        }
        const uint32_t elapsed = nowMs - lastMs_; // unsigned wrap-safe
        const uint32_t interval = moving ? params_.movingIntervalMs
                                         : params_.stationaryIntervalMs;
        if (elapsed >= interval)
        {
            return true;
        }
        if (moving && params_.minDistanceM > 0)
        {
            const uint32_t moved = Geodesy::distanceMeters(
                lastLatE7_, lastLonE7_, fix.latitudeE7, fix.longitudeE7);
            if (moved >= params_.minDistanceM)
            {
                return true;
            }
        }
        return false;
    }

    void markLogged(const Fix &fix, uint32_t nowMs)
    {
        lastLatE7_ = fix.latitudeE7;
        lastLonE7_ = fix.longitudeE7;
        lastMs_ = nowMs;
        hasLast_ = true;
    }

    void reset() { hasLast_ = false; }

    bool isAccurate(const Fix &fix) const
    {
        if (params_.minSatellites > 0 && fix.satellites < params_.minSatellites)
        {
            return false;
        }
        if (params_.maxHdopCenti > 0 && fix.hdopCenti > params_.maxHdopCenti)
        {
            return false;
        }
        return true;
    }

private:
    Params params_;
    int32_t lastLatE7_ = 0;
    int32_t lastLonE7_ = 0;
    uint32_t lastMs_ = 0;
    bool hasLast_ = false;
};
