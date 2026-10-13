#pragma once

#include <stddef.h>
#include <stdint.h>

// The tracker's top-level state machine, table-driven.
//
//   Booting ──SentenceReceived──► Acquiring ──FixValid──► Tracking
//      │                             ▲   │                  │  ▲
//      └──GnssSilent──► NoSignal ────┘   └──GnssSilent──────┘  │
//                          ▲                                   │
//                          └──────────GnssSilent───────────────┤
//   Tracking ──MotionStopped──► Stationary ──MotionDetected────┘
//   Stationary ──SleepRequested──► Sleeping ──WakeUp──► Acquiring
//
// Events the current state does not list are ignored and reported as
// "no change", so a stray event can never corrupt the state. The table is
// the single place that defines behaviour; the diagram above and the one
// in docs/ARCHITECTURE.md are drawn from it.
//
// Sleeping exists today only as the hook for the BMA400 / deep-sleep work:
// nothing requests sleep until a motion sensor that can wake the chip is
// fitted (see docs/ARCHITECTURE.md, "Adding the BMA400").
//
// Pure logic, no ESP-IDF dependency; unit-tested on the host.
enum class TrackerState : uint8_t
{
    Booting,     // no sentence seen since reset
    NoSignal,    // receiver silent for Config::gpsSilenceTimeoutMs
    Acquiring,   // sentences arrive, no valid fix
    Tracking,    // valid fix, moving
    Stationary,  // valid fix, no motion
    Sleeping,    // deep sleep requested; wake re-enters Acquiring
};

enum class TrackerEvent : uint8_t
{
    SentenceReceived,
    FixValid,
    FixLost,
    GnssSilent,
    MotionDetected,
    MotionStopped,
    SleepRequested,
    WakeUp,
};

class TrackerFsm
{
public:
    // Applies the event; true when the state changed.
    bool handle(TrackerEvent event)
    {
        for (size_t i = 0; i < transitionCount; ++i)
        {
            const Transition &t = transitions[i];
            if (t.from == state_ && t.event == event)
            {
                state_ = t.to;
                return true;
            }
        }
        return false;
    }

    TrackerState state() const { return state_; }

    bool hasFix() const
    {
        return state_ == TrackerState::Tracking || state_ == TrackerState::Stationary;
    }

    static const char *name(TrackerState state)
    {
        switch (state)
        {
        case TrackerState::Booting: return "BOOTING";
        case TrackerState::NoSignal: return "NO_SIGNAL";
        case TrackerState::Acquiring: return "ACQUIRING";
        case TrackerState::Tracking: return "TRACKING";
        case TrackerState::Stationary: return "STATIONARY";
        case TrackerState::Sleeping: return "SLEEPING";
        }
        return "?";
    }

private:
    struct Transition
    {
        TrackerState from;
        TrackerEvent event;
        TrackerState to;
    };

    static constexpr Transition transitions[] = {
        {TrackerState::Booting, TrackerEvent::SentenceReceived, TrackerState::Acquiring},
        {TrackerState::Booting, TrackerEvent::FixValid, TrackerState::Tracking},
        {TrackerState::Booting, TrackerEvent::GnssSilent, TrackerState::NoSignal},

        {TrackerState::NoSignal, TrackerEvent::SentenceReceived, TrackerState::Acquiring},
        {TrackerState::NoSignal, TrackerEvent::FixValid, TrackerState::Tracking},

        {TrackerState::Acquiring, TrackerEvent::FixValid, TrackerState::Tracking},
        {TrackerState::Acquiring, TrackerEvent::GnssSilent, TrackerState::NoSignal},

        {TrackerState::Tracking, TrackerEvent::FixLost, TrackerState::Acquiring},
        {TrackerState::Tracking, TrackerEvent::GnssSilent, TrackerState::NoSignal},
        {TrackerState::Tracking, TrackerEvent::MotionStopped, TrackerState::Stationary},

        {TrackerState::Stationary, TrackerEvent::MotionDetected, TrackerState::Tracking},
        {TrackerState::Stationary, TrackerEvent::FixLost, TrackerState::Acquiring},
        {TrackerState::Stationary, TrackerEvent::GnssSilent, TrackerState::NoSignal},
        {TrackerState::Stationary, TrackerEvent::SleepRequested, TrackerState::Sleeping},

        {TrackerState::Sleeping, TrackerEvent::WakeUp, TrackerState::Acquiring},
    };
    static constexpr size_t transitionCount = sizeof transitions / sizeof transitions[0];

    TrackerState state_ = TrackerState::Booting;
};
