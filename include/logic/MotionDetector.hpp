#pragma once

#include <stdint.h>

#include "logic/Fix.hpp"

// Where "is the tracker moving?" comes from. The FSM and the LogPolicy only
// ever see MotionChange events, so the source can be swapped:
//
//   AccelMotionDetector  — the BMA400's activity interrupt: moving on the
//                          first burst of activity pulses, stopped after a
//                          delay without one. Works without a fix and is
//                          immune to GNSS position noise. Used when the
//                          chip answers at boot.
//   SpeedMotionDetector  — GNSS ground speed with hysteresis; the fallback
//                          when no accelerometer is fitted
//   NullMotionDetector   — always moving; keeps the tracker in full-rate
//                          logging when no motion source is wanted
//
// Pure logic, no ESP-IDF dependency; unit-tested on the host.
enum class MotionChange : uint8_t
{
    None,
    Started,
    Stopped,
};

class IMotionDetector
{
public:
    virtual ~IMotionDetector() = default;

    // Called once per fix epoch (valid or not) with the monotonic time.
    virtual MotionChange update(const Fix &fix, uint32_t nowMs) = 0;
    virtual bool isMoving() const = 0;
};

// Where activity pulses come from: the BMA400 driver counts its INT1
// pulses in an ISR; the host tests inject counts directly.
class IActivitySource
{
public:
    virtual ~IActivitySource() = default;
    // Pulses since the previous call; resets the count.
    virtual uint32_t takeActivityCount() = 0;
};

// Moving once an epoch brings at least startPulses activity pulses; from
// then on any pulse keeps it moving, and stopDelayMs without one means
// parked. The two thresholds are the hysteresis: a desk bump or someone
// typing next to the tracker gives one or two pulses in a second, a car
// pulling away gives dozens, and once moving the sparse pulses of a slow
// roll are enough to stay moving.
class AccelMotionDetector final : public IMotionDetector
{
public:
    struct Params
    {
        uint32_t stopDelayMs;
        uint32_t startPulses; // per epoch (one second)
    };

    AccelMotionDetector(IActivitySource &source, const Params &params)
        : source_(source), params_(params)
    {
    }

    void setParams(const Params &params) { params_ = params; }

    MotionChange update(const Fix &, uint32_t nowMs) override
    {
        const uint32_t pulses = source_.takeActivityCount();
        totalPulses_ += pulses;
        if (!moving_)
        {
            if (pulses >= params_.startPulses && pulses > 0)
            {
                moving_ = true;
                lastActivityMs_ = nowMs;
                return MotionChange::Started;
            }
            return MotionChange::None;
        }
        if (pulses > 0)
        {
            lastActivityMs_ = nowMs;
            return MotionChange::None;
        }
        if (moving_ && nowMs - lastActivityMs_ >= params_.stopDelayMs)
        {
            moving_ = false;
            return MotionChange::Stopped;
        }
        return MotionChange::None;
    }

    bool isMoving() const override { return moving_; }
    uint32_t totalPulses() const { return totalPulses_; }

private:
    IActivitySource &source_;
    Params params_;
    bool moving_ = false;
    uint32_t lastActivityMs_ = 0;
    uint32_t totalPulses_ = 0;
};

class NullMotionDetector final : public IMotionDetector
{
public:
    MotionChange update(const Fix &, uint32_t) override { return MotionChange::None; }
    bool isMoving() const override { return true; }
};

// Moving once the speed exceeds startThreshold; stopped once it has stayed
// below stopThreshold for stopDelayMs. The two thresholds and the delay are
// the hysteresis that stops GNSS speed noise (typically 0.2..0.5 m/s while
// parked) from toggling the state every second. Invalid fixes are ignored:
// losing the fix says nothing about motion.
class SpeedMotionDetector final : public IMotionDetector
{
public:
    struct Params
    {
        uint32_t startThresholdCmPerS;
        uint32_t stopThresholdCmPerS;
        uint32_t stopDelayMs;
    };

    explicit SpeedMotionDetector(const Params &params) : params_(params) {}

    void setParams(const Params &params) { params_ = params; }

    MotionChange update(const Fix &fix, uint32_t nowMs) override
    {
        if (!fix.valid)
        {
            return MotionChange::None;
        }

        if (!moving_)
        {
            if (fix.speedCmPerS >= params_.startThresholdCmPerS)
            {
                moving_ = true;
                return MotionChange::Started;
            }
            return MotionChange::None;
        }

        if (fix.speedCmPerS >= params_.stopThresholdCmPerS)
        {
            belowSince_ = nowMs;
            belowStarted_ = false;
            return MotionChange::None;
        }
        if (!belowStarted_)
        {
            belowStarted_ = true;
            belowSince_ = nowMs;
            return MotionChange::None;
        }
        if (nowMs - belowSince_ >= params_.stopDelayMs)
        {
            moving_ = false;
            belowStarted_ = false;
            return MotionChange::Stopped;
        }
        return MotionChange::None;
    }

    bool isMoving() const override { return moving_; }

private:
    Params params_;
    bool moving_ = false;
    bool belowStarted_ = false;
    uint32_t belowSince_ = 0;
};
