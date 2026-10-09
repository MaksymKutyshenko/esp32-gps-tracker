#pragma once

#include <stdint.h>

#include "logic/Fix.hpp"

// Where "is the tracker moving?" comes from. The FSM and the LogPolicy only
// ever see MotionChange events, so the source can be swapped:
//
//   SpeedMotionDetector  — today: GNSS ground speed with hysteresis
//   Bma400MotionDetector — later: the accelerometer's activity interrupt,
//                          which also works without a fix and can wake the
//                          chip from deep sleep (see docs/ARCHITECTURE.md,
//                          "Adding the BMA400")
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
