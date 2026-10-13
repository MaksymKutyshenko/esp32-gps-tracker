#pragma once

#include <stdint.h>

#include "logic/TrackerFsm.hpp"

// When a parked tracker may go to deep sleep: after afterStationaryMs in
// the Stationary state, once the uplink has nothing left to send, so the
// last points and the "sleeping" health report are not lost. The wait for
// the uplink is bounded by uplinkGraceMs: without coverage the queue never
// drains, and a tracker that stays awake for that would flatten its
// battery in a garage. Past the grace period it sleeps anyway; the card
// has every point, the health report shows the backlog that was left.
// Any other state ends the parked period; afterStationaryMs 0 disables
// sleep altogether.
//
// Pure logic, no ESP-IDF dependency; unit-tested on the host.
class SleepPolicy
{
public:
    struct Params
    {
        uint32_t afterStationaryMs;
        uint32_t uplinkGraceMs; // beyond afterStationaryMs, waiting for the queue
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
        if (params_.afterStationaryMs == 0 || !parked_)
        {
            return false;
        }
        // The epoch's timestamp can be a few ms older than the one the
        // transition was stamped with (it is taken before the FSM runs), so
        // an unsigned difference would wrap to "parked for 49 days". A
        // signed difference reads that as "not yet".
        const int32_t parkedForMs = static_cast<int32_t>(nowMs - parkedSinceMs_);
        if (parkedForMs < static_cast<int32_t>(params_.afterStationaryMs))
        {
            return false;
        }
        return uplinkIdle ||
               parkedForMs >= static_cast<int32_t>(params_.afterStationaryMs + params_.uplinkGraceMs);
    }

private:
    Params params_;
    bool parked_ = false;
    uint32_t parkedSinceMs_ = 0;
};
