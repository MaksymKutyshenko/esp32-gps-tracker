// EmaFilter, Geodesy, LogPolicy, TrackRecord formatting.
#include <string.h>

#include <unity.h>

#include "logic/Calendar.hpp"
#include "logic/EmaFilter.hpp"
#include "logic/Geodesy.hpp"
#include "logic/LogPolicy.hpp"
#include "logic/TrackRecord.hpp"

// --- EmaFilter -----------------------------------------------------------

void ema_first_sample_seeds_without_transient()
{
    EmaFilter<int32_t, 1, 5> filter;
    TEST_ASSERT_FALSE(filter.seeded());
    TEST_ASSERT_EQUAL_INT32(1000, filter.push(1000));
    TEST_ASSERT_TRUE(filter.seeded());
}

void ema_steps_toward_input_by_alpha()
{
    EmaFilter<int32_t, 1, 5> filter; // alpha 0.2
    filter.push(0);
    TEST_ASSERT_EQUAL_INT32(200, filter.push(1000));
    TEST_ASSERT_EQUAL_INT32(360, filter.push(1000));
    TEST_ASSERT_EQUAL_INT32(488, filter.push(1000));
}

void ema_is_identity_for_constant_input_and_handles_negative()
{
    EmaFilter<int32_t, 1, 4> filter;
    for (int i = 0; i < 10; ++i)
    {
        TEST_ASSERT_EQUAL_INT32(-250, filter.push(-250));
    }
}

void ema_reset_reseeds()
{
    EmaFilter<int32_t, 1, 2> filter;
    filter.push(100);
    filter.reset();
    TEST_ASSERT_EQUAL_INT32(7, filter.push(7));
}

// --- Geodesy -------------------------------------------------------------

void geodesy_same_point_is_zero()
{
    TEST_ASSERT_EQUAL_UINT32(0, Geodesy::distanceMeters(504520575, 305257202, 504520575, 305257202));
}

void geodesy_one_hundredth_degree_of_latitude_is_about_1112_m()
{
    // 0.01 deg of latitude on a 6371 km sphere = 1111.95 m, independent of longitude.
    uint32_t d = Geodesy::distanceMeters(500000000, 300000000, 500100000, 300000000);
    TEST_ASSERT_UINT32_WITHIN(2, 1112, d);
}

void geodesy_longitude_shrinks_with_latitude()
{
    // 0.01 deg of longitude at 60 N is half of what it is at the equator.
    uint32_t equator = Geodesy::distanceMeters(0, 0, 0, 100000);
    uint32_t sixty = Geodesy::distanceMeters(600000000, 0, 600000000, 100000);
    TEST_ASSERT_UINT32_WITHIN(2, 1112, equator);
    TEST_ASSERT_UINT32_WITHIN(2, 556, sixty);
}

// --- LogPolicy -----------------------------------------------------------

namespace
{
    Fix fixAt(int32_t latE7, int32_t lonE7)
    {
        Fix fix = {};
        fix.valid = true;
        fix.latitudeE7 = latE7;
        fix.longitudeE7 = lonE7;
        return fix;
    }
    const LogPolicy::Params params = {5000, 60'000, 50, 0, 0}; // gates off
}

void logpolicy_never_logs_invalid_fix()
{
    LogPolicy policy(params);
    Fix fix = fixAt(0, 0);
    fix.valid = false;
    TEST_ASSERT_FALSE(policy.shouldLog(fix, 0, true));
}

void logpolicy_skips_fixes_with_few_satellites_or_poor_hdop()
{
    LogPolicy policy({5000, 60'000, 50, 5, 300});
    Fix fix = fixAt(0, 0);
    fix.satellites = 4;
    fix.hdopCenti = 120;
    TEST_ASSERT_FALSE(policy.shouldLog(fix, 0, true)); // one satellite short
    fix.satellites = 5;
    fix.hdopCenti = 301;
    TEST_ASSERT_FALSE(policy.shouldLog(fix, 0, true)); // geometry too poor
    fix.hdopCenti = 300;
    TEST_ASSERT_TRUE(policy.shouldLog(fix, 0, true));  // both at the limit
}

void logpolicy_logs_first_valid_fix_immediately()
{
    LogPolicy policy(params);
    TEST_ASSERT_TRUE(policy.shouldLog(fixAt(500000000, 300000000), 123, false));
}

void logpolicy_moving_uses_short_interval()
{
    LogPolicy policy(params);
    Fix here = fixAt(500000000, 300000000);
    policy.markLogged(here, 0);
    TEST_ASSERT_FALSE(policy.shouldLog(here, 4999, true));
    TEST_ASSERT_TRUE(policy.shouldLog(here, 5000, true));
}

void logpolicy_stationary_uses_long_interval()
{
    LogPolicy policy(params);
    Fix here = fixAt(500000000, 300000000);
    policy.markLogged(here, 0);
    TEST_ASSERT_FALSE(policy.shouldLog(here, 5000, false));
    TEST_ASSERT_FALSE(policy.shouldLog(here, 59'999, false));
    TEST_ASSERT_TRUE(policy.shouldLog(here, 60'000, false));
}

void logpolicy_distance_triggers_early_while_moving()
{
    LogPolicy policy(params);
    policy.markLogged(fixAt(500000000, 300000000), 0);
    Fix moved = fixAt(500005000, 300000000); // 0.0005 deg = ~56 m north
    TEST_ASSERT_TRUE(policy.shouldLog(moved, 1000, true));
    TEST_ASSERT_FALSE(policy.shouldLog(moved, 1000, false)); // distance ignored when stationary
}

void logpolicy_handles_tick_wraparound()
{
    LogPolicy policy(params);
    policy.markLogged(fixAt(0, 0), 0xFFFFF000u);
    TEST_ASSERT_FALSE(policy.shouldLog(fixAt(0, 0), 0xFFFFF800u, true));
    TEST_ASSERT_TRUE(policy.shouldLog(fixAt(0, 0), 0x00000800u, true)); // 6144 ms later
}

// --- TrackRecord ---------------------------------------------------------

void record_formats_fixed_point_with_sign_and_padding()
{
    char out[32];
    TEST_ASSERT_EQUAL_size_t(10, TrackRecordFormat::formatFixed(out, sizeof out, 504520575, 7));
    TEST_ASSERT_EQUAL_STRING("50.4520575", out);
    TrackRecordFormat::formatFixed(out, sizeof out, -5000000, 7);
    TEST_ASSERT_EQUAL_STRING("-0.5000000", out);
    TrackRecordFormat::formatFixed(out, sizeof out, 95, 2);
    TEST_ASSERT_EQUAL_STRING("0.95", out);
    TrackRecordFormat::formatFixed(out, sizeof out, 7, 0);
    TEST_ASSERT_EQUAL_STRING("7", out);
    TEST_ASSERT_EQUAL_size_t(0, TrackRecordFormat::formatFixed(out, 4, 504520575, 7));
}

void record_csv_line_matches_expected_layout()
{
    Fix fix = {};
    fix.valid = true;
    fix.latitudeE7 = 504520575;
    fix.longitudeE7 = 305257202;
    fix.altitudeCm = 17930;
    fix.speedCmPerS = 635;
    fix.courseDeciDeg = 2105;
    fix.hdopCenti = 95;
    fix.satellites = 8;
    fix.quality = 1;
    fix.utc = {2026, 10, 9, 10, 30, 45};

    TrackRecord record = TrackRecord::from(fix, 3900);
    char line[128];
    size_t n = TrackRecordFormat::csvLine(record, line, sizeof line);
    TEST_ASSERT_EQUAL_STRING(
        "2026-10-09T10:30:45Z,50.4520575,30.5257202,179.30,22.8,210.5,8,0.95,3900\r\n", line);
    TEST_ASSERT_EQUAL_size_t(strlen(line), n);
}

void record_csv_line_reports_overflow_as_zero()
{
    TrackRecord record = TrackRecord::from(Fix{}, 0);
    char line[20];
    TEST_ASSERT_EQUAL_size_t(0, TrackRecordFormat::csvLine(record, line, sizeof line));
}

// --- Calendar / TimeZone ----------------------------------------------------

void calendar_rejects_a_date_from_an_unsettled_receiver()
{
    UtcTime today = {2026, 10, 14, 20, 52, 53};
    TEST_ASSERT_TRUE(Calendar::plausible(today, 2025, 2045));
    UtcTime coldStart = {2087, 7, 15, 20, 51, 32}; // seen on the bench
    TEST_ASSERT_FALSE(Calendar::plausible(coldStart, 2025, 2045));
    UtcTime unset = {2000, 0, 0, 0, 0, 0};
    TEST_ASSERT_FALSE(Calendar::plausible(unset, 2025, 2045));
}

void calendar_round_trips_unix_time()
{
    const UtcTime t = {2026, 10, 9, 10, 30, 45};
    const int64_t seconds = Calendar::unixTime(t);
    TEST_ASSERT_EQUAL_INT64(1791541845, seconds);
    const UtcTime back = Calendar::fromUnixTime(seconds);
    TEST_ASSERT_EQUAL_UINT16(2026, back.year);
    TEST_ASSERT_EQUAL_UINT8(10, back.month);
    TEST_ASSERT_EQUAL_UINT8(9, back.day);
    TEST_ASSERT_EQUAL_UINT8(10, back.hour);
    TEST_ASSERT_EQUAL_UINT8(30, back.minute);
    TEST_ASSERT_EQUAL_UINT8(45, back.second);
    TEST_ASSERT_EQUAL_UINT8(29, Calendar::lastSunday(2026, 3));  // 2026-03-29
    TEST_ASSERT_EQUAL_UINT8(25, Calendar::lastSunday(2026, 10)); // 2026-10-25
}

void timezone_kyiv_switches_on_the_european_dates_and_rolls_the_day()
{
    TimeZone kyiv({120, true});
    TEST_ASSERT_EQUAL_INT32(180, kyiv.offsetMinutes({2026, 10, 12, 10, 0, 0})); // summer
    TEST_ASSERT_EQUAL_INT32(120, kyiv.offsetMinutes({2026, 1, 15, 10, 0, 0}));  // winter
    TEST_ASSERT_EQUAL_INT32(120, kyiv.offsetMinutes({2026, 3, 29, 0, 59, 59})); // minute before the switch
    TEST_ASSERT_EQUAL_INT32(180, kyiv.offsetMinutes({2026, 3, 29, 1, 0, 0}));
    TEST_ASSERT_EQUAL_INT32(180, kyiv.offsetMinutes({2026, 10, 25, 0, 59, 59}));
    TEST_ASSERT_EQUAL_INT32(120, kyiv.offsetMinutes({2026, 10, 25, 1, 0, 0}));

    const UtcTime local = kyiv.toLocal({2026, 10, 12, 22, 30, 0}); // 22:30Z -> 01:30 next day
    TEST_ASSERT_EQUAL_UINT8(13, local.day);
    TEST_ASSERT_EQUAL_UINT8(1, local.hour);
    TEST_ASSERT_EQUAL_UINT8(30, local.minute);

    TimeZone fixed({120, false});
    TEST_ASSERT_EQUAL_INT32(120, fixed.offsetMinutes({2026, 7, 1, 12, 0, 0}));
}
