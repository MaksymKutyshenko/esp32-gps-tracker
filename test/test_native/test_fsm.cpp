// TrackerFsm transition table and MotionDetector hysteresis.
#include <unity.h>

#include "logic/MotionDetector.hpp"
#include "logic/TrackerFsm.hpp"

void fsm_starts_booting_and_acquires_on_first_sentence()
{
    TrackerFsm fsm;
    TEST_ASSERT_EQUAL(TrackerState::Booting, fsm.state());
    TEST_ASSERT_TRUE(fsm.handle(TrackerEvent::SentenceReceived));
    TEST_ASSERT_EQUAL(TrackerState::Acquiring, fsm.state());
    TEST_ASSERT_FALSE(fsm.hasFix());
}

void fsm_tracks_on_valid_fix_and_returns_to_acquiring_when_lost()
{
    TrackerFsm fsm;
    fsm.handle(TrackerEvent::SentenceReceived);
    TEST_ASSERT_TRUE(fsm.handle(TrackerEvent::FixValid));
    TEST_ASSERT_EQUAL(TrackerState::Tracking, fsm.state());
    TEST_ASSERT_TRUE(fsm.hasFix());
    TEST_ASSERT_TRUE(fsm.handle(TrackerEvent::FixLost));
    TEST_ASSERT_EQUAL(TrackerState::Acquiring, fsm.state());
}

void fsm_silence_leads_to_no_signal_from_any_live_state()
{
    const TrackerEvent paths[][2] = {
        {TrackerEvent::SentenceReceived, TrackerEvent::GnssSilent},
        {TrackerEvent::FixValid, TrackerEvent::GnssSilent},
    };
    for (const auto &path : paths)
    {
        TrackerFsm fsm;
        fsm.handle(path[0]);
        TEST_ASSERT_TRUE(fsm.handle(path[1]));
        TEST_ASSERT_EQUAL(TrackerState::NoSignal, fsm.state());
        TEST_ASSERT_TRUE(fsm.handle(TrackerEvent::SentenceReceived));
        TEST_ASSERT_EQUAL(TrackerState::Acquiring, fsm.state());
    }
}

void fsm_motion_toggles_tracking_and_stationary()
{
    TrackerFsm fsm;
    fsm.handle(TrackerEvent::FixValid);
    TEST_ASSERT_TRUE(fsm.handle(TrackerEvent::MotionStopped));
    TEST_ASSERT_EQUAL(TrackerState::Stationary, fsm.state());
    TEST_ASSERT_TRUE(fsm.hasFix());
    TEST_ASSERT_TRUE(fsm.handle(TrackerEvent::MotionDetected));
    TEST_ASSERT_EQUAL(TrackerState::Tracking, fsm.state());
}

void fsm_ignores_events_not_in_table()
{
    TrackerFsm fsm;
    TEST_ASSERT_FALSE(fsm.handle(TrackerEvent::MotionStopped)); // Booting
    TEST_ASSERT_FALSE(fsm.handle(TrackerEvent::SleepRequested));
    TEST_ASSERT_EQUAL(TrackerState::Booting, fsm.state());
    fsm.handle(TrackerEvent::FixValid);
    TEST_ASSERT_FALSE(fsm.handle(TrackerEvent::SleepRequested)); // only from Stationary
    TEST_ASSERT_EQUAL(TrackerState::Tracking, fsm.state());
}

void fsm_sleep_only_from_stationary_and_wakes_into_acquiring()
{
    TrackerFsm fsm;
    fsm.handle(TrackerEvent::FixValid);
    fsm.handle(TrackerEvent::MotionStopped);
    TEST_ASSERT_TRUE(fsm.handle(TrackerEvent::SleepRequested));
    TEST_ASSERT_EQUAL(TrackerState::Sleeping, fsm.state());
    TEST_ASSERT_FALSE(fsm.hasFix());
    TEST_ASSERT_TRUE(fsm.handle(TrackerEvent::WakeUp));
    TEST_ASSERT_EQUAL(TrackerState::Acquiring, fsm.state());
}

void fsm_state_names_are_stable()
{
    TEST_ASSERT_EQUAL_STRING("TRACKING", TrackerFsm::name(TrackerState::Tracking));
    TEST_ASSERT_EQUAL_STRING("NO_SIGNAL", TrackerFsm::name(TrackerState::NoSignal));
}

// --- motion detectors ----------------------------------------------------

namespace
{
    Fix fixWithSpeed(uint32_t cmPerS)
    {
        Fix fix = {};
        fix.valid = true;
        fix.speedCmPerS = cmPerS;
        return fix;
    }
}

void motion_speed_detector_starts_above_threshold()
{
    SpeedMotionDetector detector({150, 50, 10'000});
    TEST_ASSERT_FALSE(detector.isMoving());
    TEST_ASSERT_EQUAL(MotionChange::None, detector.update(fixWithSpeed(100), 0));
    TEST_ASSERT_EQUAL(MotionChange::Started, detector.update(fixWithSpeed(150), 1000));
    TEST_ASSERT_TRUE(detector.isMoving());
    TEST_ASSERT_EQUAL(MotionChange::None, detector.update(fixWithSpeed(300), 2000));
}

void motion_speed_detector_stops_only_after_delay_below_lower_threshold()
{
    SpeedMotionDetector detector({150, 50, 10'000});
    detector.update(fixWithSpeed(200), 0);

    // Between the two thresholds: still moving, no timer.
    TEST_ASSERT_EQUAL(MotionChange::None, detector.update(fixWithSpeed(100), 1000));
    // Below the stop threshold: timer starts.
    TEST_ASSERT_EQUAL(MotionChange::None, detector.update(fixWithSpeed(10), 2000));
    TEST_ASSERT_EQUAL(MotionChange::None, detector.update(fixWithSpeed(10), 11'000));
    TEST_ASSERT_EQUAL(MotionChange::Stopped, detector.update(fixWithSpeed(10), 12'000));
    TEST_ASSERT_FALSE(detector.isMoving());
}

void motion_speed_detector_brief_stop_does_not_count()
{
    SpeedMotionDetector detector({150, 50, 10'000});
    detector.update(fixWithSpeed(200), 0);
    detector.update(fixWithSpeed(10), 1000);
    detector.update(fixWithSpeed(200), 5000); // moved again, timer reset
    TEST_ASSERT_EQUAL(MotionChange::None, detector.update(fixWithSpeed(10), 12'000));
    TEST_ASSERT_TRUE(detector.isMoving());
}

void motion_speed_detector_ignores_invalid_fixes()
{
    SpeedMotionDetector detector({150, 50, 10'000});
    detector.update(fixWithSpeed(200), 0);
    Fix lost = fixWithSpeed(0);
    lost.valid = false;
    TEST_ASSERT_EQUAL(MotionChange::None, detector.update(lost, 60'000));
    TEST_ASSERT_TRUE(detector.isMoving());
}

void motion_null_detector_always_moving()
{
    NullMotionDetector detector;
    TEST_ASSERT_TRUE(detector.isMoving());
    TEST_ASSERT_EQUAL(MotionChange::None, detector.update(fixWithSpeed(0), 0));
}
