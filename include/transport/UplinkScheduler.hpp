#pragma once

#include <stdint.h>

// When the uplink task may try the next request. Successes are paced by a
// minimum interval (airtime and battery); failures back off exponentially
// up to a cap, and after a configurable run of failures the task is told
// to reset the link. This is the regulation loop on the radio side: the
// measured outcome of each request sets the next attempt.
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
    }

    void onFailure(uint32_t nowMs)
    {
        lastAttemptMs_ = nowMs;
        hasAttempt_ = true;
        if (consecutiveFailures_ < 255)
        {
            ++consecutiveFailures_;
        }
        uint64_t next = static_cast<uint64_t>(params_.backoffInitialMs)
                        << (consecutiveFailures_ > 16 ? 16 : consecutiveFailures_ - 1);
        waitMs_ = next > params_.backoffMaxMs ? params_.backoffMaxMs : static_cast<uint32_t>(next);
    }

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
    Params params_;
    uint32_t lastAttemptMs_ = 0;
    uint32_t waitMs_ = 0;
    uint8_t consecutiveFailures_ = 0;
    bool hasAttempt_ = false;
};
