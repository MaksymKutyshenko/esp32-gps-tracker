#pragma once

#include <stdint.h>

// When the uplink task may try the next request. Successes are paced by a
// minimum interval (airtime and battery); failures back off exponentially
// up to a cap, and after a configurable run of link failures the task is
// told to reset the link. A request the server answered but refused (an
// HTTP status other than 200) backs off the same way but never counts
// toward the reset: the link works, and power-cycling the modem would only
// make it re-attach for nothing. This is the regulation loop on the radio
// side: the measured outcome of each request sets the next attempt.
//
// Pure logic, no ESP-IDF dependency; unit-tested on the host.
class UplinkScheduler
{
public:
    struct Params
    {
        uint32_t minIntervalMs;
        uint32_t backoffInitialMs;
        uint32_t backoffMaxMs;
        uint8_t failuresBeforeReset;
    };

    explicit UplinkScheduler(const Params &params) : params_(params) {}

    void setParams(const Params &params) { params_ = params; }

    bool canAttempt(uint32_t nowMs) const
    {
        return !hasAttempt_ || nowMs - lastAttemptMs_ >= waitMs_;
    }

    void onSuccess(uint32_t nowMs)
    {
        lastAttemptMs_ = nowMs;
        hasAttempt_ = true;
        waitMs_ = params_.minIntervalMs;
        consecutiveFailures_ = 0;
        backoffSteps_ = 0;
    }

    // No answer: timeout, modem error, dead bearer.
    void onFailure(uint32_t nowMs)
    {
        backOff(nowMs);
        if (consecutiveFailures_ < 255)
        {
            ++consecutiveFailures_;
        }
    }

    // Answered and refused: the server is reachable, the request is not
    // wanted (unknown device, bad query). Retrying later is all we can do.
    void onRejected(uint32_t nowMs) { backOff(nowMs); }

    bool needsReset() const
    {
        return params_.failuresBeforeReset > 0 &&
               consecutiveFailures_ >= params_.failuresBeforeReset;
    }

    // After the link was reset the failure run starts over; the back-off
    // wait is kept so a dead network is not hammered.
    void onReset() { consecutiveFailures_ = 0; }

    uint8_t consecutiveFailures() const { return consecutiveFailures_; }
    uint32_t currentWaitMs() const { return waitMs_; }

private:
    void backOff(uint32_t nowMs)
    {
        lastAttemptMs_ = nowMs;
        hasAttempt_ = true;
        if (backoffSteps_ < 16)
        {
            ++backoffSteps_;
        }
        const uint64_t next = static_cast<uint64_t>(params_.backoffInitialMs) << (backoffSteps_ - 1);
        waitMs_ = next > params_.backoffMaxMs ? params_.backoffMaxMs : static_cast<uint32_t>(next);
    }

    Params params_;
    uint32_t lastAttemptMs_ = 0;
    uint32_t waitMs_ = 0;
    uint8_t consecutiveFailures_ = 0; // link failures only
    uint8_t backoffSteps_ = 0;        // failures and rejections
    bool hasAttempt_ = false;
};
