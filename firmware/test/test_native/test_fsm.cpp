// TrackerFsm transition table and MotionDetector hysteresis.
#include <unity.h>

#include "logic/MotionDetector.hpp"
#include "logic/SleepPolicy.hpp"
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

namespace
{
    // Host stand-in for the BMA400's pulse counter.
    class FakeActivity final : public IActivitySource
    {
    public:
        uint32_t pending = 0;
        uint32_t takeActivityCount() override
        {
            const uint32_t n = pending;
            pending = 0;
            return n;
        }
    };
}

void motion_accel_detector_starts_on_first_pulse_and_stops_after_delay()
{
    FakeActivity source;
    AccelMotionDetector detector(source, {30'000, 3});
    const Fix any = fixWithSpeed(0);

    TEST_ASSERT_EQUAL(MotionChange::None, detector.update(any, 0));
    TEST_ASSERT_FALSE(detector.isMoving());

    // A bump of two pulses is below the start threshold of three.
    source.pending = 2;
    TEST_ASSERT_EQUAL(MotionChange::None, detector.update(any, 500));
    TEST_ASSERT_FALSE(detector.isMoving());

    source.pending = 3;
    TEST_ASSERT_EQUAL(MotionChange::Started, detector.update(any, 1000));
    TEST_ASSERT_TRUE(detector.isMoving());
    TEST_ASSERT_EQUAL_UINT32(5, detector.totalPulses());

    // Quiet for less than the delay: still moving.
    TEST_ASSERT_EQUAL(MotionChange::None, detector.update(any, 20'000));
    TEST_ASSERT_TRUE(detector.isMoving());
    // A pulse inside the delay restarts it.
    source.pending = 1;
    TEST_ASSERT_EQUAL(MotionChange::None, detector.update(any, 25'000));
    TEST_ASSERT_EQUAL(MotionChange::None, detector.update(any, 54'000));
    TEST_ASSERT_EQUAL(MotionChange::Stopped, detector.update(any, 55'000));
    TEST_ASSERT_FALSE(detector.isMoving());
}

void motion_accel_detector_ignores_the_fix_entirely()
{
    FakeActivity source;
    AccelMotionDetector detector(source, {1000, 1});
    Fix invalid = fixWithSpeed(500);
    invalid.valid = false;
    source.pending = 1;
    TEST_ASSERT_EQUAL(MotionChange::Started, detector.update(invalid, 0)); // no fix needed
    TEST_ASSERT_EQUAL(MotionChange::None, detector.update(fixWithSpeed(500), 500)); // speed irrelevant
    TEST_ASSERT_EQUAL(MotionChange::Stopped, detector.update(fixWithSpeed(500), 1000));
}

void motion_null_detector_always_moving()
{
    NullMotionDetector detector;
    TEST_ASSERT_TRUE(detector.isMoving());
    TEST_ASSERT_EQUAL(MotionChange::None, detector.update(fixWithSpeed(0), 0));
}

// --- SleepPolicy -----------------------------------------------------------

void sleeppolicy_due_after_parked_long_enough_with_idle_uplink()
{
    SleepPolicy policy({600'000, 180'000});
    policy.onState(TrackerState::Tracking, 0);
    TEST_ASSERT_FALSE(policy.due(10'000'000, true)); // never parked
    policy.onState(TrackerState::Stationary, 1000);
    TEST_ASSERT_FALSE(policy.due(600'999, true));
    TEST_ASSERT_TRUE(policy.due(601'000, true));
    TEST_ASSERT_FALSE(policy.due(601'000, false)); // uploads still waiting
}

void sleeppolicy_motion_restarts_the_parked_period_and_zero_disables()
{
    SleepPolicy policy({600'000, 180'000});
    policy.onState(TrackerState::Stationary, 0);
    policy.onState(TrackerState::Tracking, 300'000);
    policy.onState(TrackerState::Stationary, 400'000);
    TEST_ASSERT_FALSE(policy.due(900'000, true));
    TEST_ASSERT_TRUE(policy.due(1'000'000, true));

    policy.setParams({0, 0});
    TEST_ASSERT_FALSE(policy.due(10'000'000, true));
}

void sleeppolicy_sleeps_anyway_once_the_uplink_grace_period_is_over()
{
    // No coverage: the queue never drains. The tracker waits the grace
    // period past the parked time, then sleeps with the backlog on the card.
    SleepPolicy policy({600'000, 180'000});
    policy.onState(TrackerState::Stationary, 0);
    TEST_ASSERT_FALSE(policy.due(600'000, false));
    TEST_ASSERT_FALSE(policy.due(779'999, false));
    TEST_ASSERT_TRUE(policy.due(780'000, false));
    // Motion in between restarts both periods.
    policy.onState(TrackerState::Tracking, 700'000);
    policy.onState(TrackerState::Stationary, 700'000);
    TEST_ASSERT_FALSE(policy.due(1'479'999, false));
    TEST_ASSERT_TRUE(policy.due(1'480'000, false));
}

void sleeppolicy_is_not_fooled_by_an_epoch_stamped_before_the_transition()
{
    // The gps task stamps the epoch, then runs the FSM, which stamps the
    // transition a few ms later; asking with the older epoch time must
    // not read as "parked for ages".
    SleepPolicy policy({600'000, 180'000});
    policy.onState(TrackerState::Stationary, 1051);
    TEST_ASSERT_FALSE(policy.due(1000, true));
    TEST_ASSERT_FALSE(policy.due(601'050, true));
    TEST_ASSERT_TRUE(policy.due(601'051, true));
}
