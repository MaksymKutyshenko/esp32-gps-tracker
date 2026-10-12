#pragma once

#include <stdint.h>

#include "logic/TrackerFsm.hpp"

// When a parked tracker may go to deep sleep: after afterStationaryMs in
// the Stationary state, and only once the uplink has nothing left to send,
// so the last heartbeat and the "sleeping" health report are not lost.
// Any other state ends the parked period; 0 disables sleep altogether.
//
// Pure logic, no ESP-IDF dependency; unit-tested on the host.
class SleepPolicy
{
public:
    struct Params
    {
        uint32_t afterStationaryMs;
    };

    explicit SleepPolicy(const Params &params) : params_(params) {}

    void setParams(const Params &params) { params_ = params; }

    // Called on every FSM transition with the new state.
    void onState(TrackerState state, uint32_t nowMs)
    {
        if (state != TrackerState::Stationary)
        {
            parked_ = false;
            return;
        }
        if (!parked_)
        {
            parked_ = true;
            parkedSinceMs_ = nowMs;
        }
    }

    bool due(uint32_t nowMs, bool uplinkIdle) const
    {
        return params_.afterStationaryMs > 0 && parked_ && uplinkIdle &&
               nowMs - parkedSinceMs_ >= params_.afterStationaryMs;
    }

private:
    Params params_;
    bool parked_ = false;
    uint32_t parkedSinceMs_ = 0;
};
