// Transport policies on the host: TrackLogger retry/recovery with a fake
// storage, OsmAnd URL building, UplinkScheduler back-off, ButtonDecoder.
#include <string.h>

#include <unity.h>

#include "logic/ButtonDecoder.hpp"
#include "transport/OsmAndProtocol.hpp"
#include "transport/TrackLogger.hpp"
#include "transport/UplinkScheduler.hpp"

namespace
{
    // In-memory IStorage that can be told to fail the next N appends and
    // whether recovery works.
    class FakeStorage final : public IStorage
    {
    public:
        bool isReady() const override { return ready; }

        esp_err_t append(const char *path, const char *header,
                         const char *line, size_t length) override
        {
            ++appendCalls;
            if (failRemaining > 0)
            {
                --failRemaining;
                return ESP_FAIL;
            }
            strncpy(lastPath, path, sizeof lastPath - 1);
            if (!headerSeen)
            {
                strncpy(content, header, sizeof content - 1);
                headerSeen = true;
            }
            strncat(content, line, length);
            return ESP_OK;
        }

        esp_err_t recover() override
        {
            ++recoverCalls;
            if (recoverSucceeds)
            {
                failRemaining = 0;
                ready = true;
                return ESP_OK;
            }
            ready = false;
            return ESP_FAIL;
        }

        bool ready = true;
        bool recoverSucceeds = true;
        uint32_t failRemaining = 0;
        uint32_t appendCalls = 0;
        uint32_t recoverCalls = 0;
        bool headerSeen = false;
        char lastPath[64] = {};
        char content[512] = {};
    };

    TrackRecord sampleRecord()
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
        fix.utc = {2026, 10, 9, 10, 30, 45};
        return TrackRecord::from(fix, 3900);
    }
}

// --- TrackLogger ---------------------------------------------------------

void logger_writes_header_then_line_into_daily_file()
{
    FakeStorage storage;
    TrackLogger logger(storage, "/sdcard/TRACKS", 3);
    TEST_ASSERT_EQUAL(ESP_OK, logger.log(sampleRecord()));
    TEST_ASSERT_EQUAL_STRING("/sdcard/TRACKS/20261009.CSV", storage.lastPath);
    TEST_ASSERT_EQUAL_STRING(
        "utc,lat,lon,alt_m,speed_kmh,course_deg,sats,hdop,batt_mv\r\n"
        "2026-10-09T10:30:45Z,50.4520575,30.5257202,179.30,22.8,210.5,8,0.95,3900\r\n",
        storage.content);
    TEST_ASSERT_EQUAL_UINT32(1, logger.written());
}

void logger_retries_transient_failure_without_recovery()
{
    FakeStorage storage;
    storage.failRemaining = 2;
    TrackLogger logger(storage, "/t", 3);
    TEST_ASSERT_EQUAL(ESP_OK, logger.log(sampleRecord()));
    TEST_ASSERT_EQUAL_UINT32(3, storage.appendCalls);
    TEST_ASSERT_EQUAL_UINT32(0, storage.recoverCalls);
    TEST_ASSERT_EQUAL_UINT32(2, logger.appendFailures());
    TEST_ASSERT_EQUAL_UINT32(0, logger.lost());
}

void logger_recovers_after_exhausting_retries()
{
    FakeStorage storage;
    storage.failRemaining = 3;
    TrackLogger logger(storage, "/t", 3);
    TEST_ASSERT_EQUAL(ESP_OK, logger.log(sampleRecord()));
    TEST_ASSERT_EQUAL_UINT32(4, storage.appendCalls); // 3 failed + 1 after recovery
    TEST_ASSERT_EQUAL_UINT32(1, storage.recoverCalls);
    TEST_ASSERT_EQUAL_UINT32(1, logger.recoveries());
    TEST_ASSERT_EQUAL_UINT32(1, logger.written());
}

void logger_reports_loss_when_recovery_fails()
{
    FakeStorage storage;
    storage.failRemaining = 10;
    storage.recoverSucceeds = false;
    TrackLogger logger(storage, "/t", 3);
    TEST_ASSERT_EQUAL(ESP_FAIL, logger.log(sampleRecord()));
    TEST_ASSERT_EQUAL_UINT32(1, logger.lost());
    TEST_ASSERT_EQUAL_UINT32(0, logger.written());
}

// --- OsmAndProtocol ------------------------------------------------------

void osmand_unix_time_matches_known_dates()
{
    TEST_ASSERT_EQUAL_INT64(0, OsmAndProtocol::unixTime({1970, 1, 1, 0, 0, 0}));
    TEST_ASSERT_EQUAL_INT64(946684800, OsmAndProtocol::unixTime({2000, 1, 1, 0, 0, 0}));
    TEST_ASSERT_EQUAL_INT64(1791541845, OsmAndProtocol::unixTime({2026, 10, 9, 10, 30, 45}));
}

void osmand_battery_percent_is_clamped_linear()
{
    TEST_ASSERT_EQUAL_UINT8(0, OsmAndProtocol::batteryPercent(3000));
    TEST_ASSERT_EQUAL_UINT8(0, OsmAndProtocol::batteryPercent(3300));
    TEST_ASSERT_EQUAL_UINT8(50, OsmAndProtocol::batteryPercent(3750));
    TEST_ASSERT_EQUAL_UINT8(100, OsmAndProtocol::batteryPercent(4200));
    TEST_ASSERT_EQUAL_UINT8(100, OsmAndProtocol::batteryPercent(4300));
}

void osmand_url_carries_every_field()
{
    char url[192];
    size_t n = OsmAndProtocol::buildUrl("http://demo.traccar.org:5055", "trk-1",
                                        sampleRecord(), url, sizeof url);
    TEST_ASSERT_EQUAL_STRING(
        "http://demo.traccar.org:5055/?id=trk-1&lat=50.4520575&lon=30.5257202"
        "&timestamp=1791541845&speed=12.34&bearing=210.5&altitude=179.30&hdop=0.95&batt=66",
        url);
    TEST_ASSERT_EQUAL_size_t(strlen(url), n);
}

void osmand_url_too_long_returns_zero()
{
    char url[40];
    TEST_ASSERT_EQUAL_size_t(0, OsmAndProtocol::buildUrl("http://h", "d", sampleRecord(), url, sizeof url));
}

// --- UplinkScheduler -----------------------------------------------------

void scheduler_first_attempt_is_immediate_then_paced_by_min_interval()
{
    UplinkScheduler scheduler({10'000, 1000, 60'000, 3});
    TEST_ASSERT_TRUE(scheduler.canAttempt(0));
    scheduler.onSuccess(0);
    TEST_ASSERT_FALSE(scheduler.canAttempt(9999));
    TEST_ASSERT_TRUE(scheduler.canAttempt(10'000));
}

void scheduler_backs_off_exponentially_up_to_cap()
{
    UplinkScheduler scheduler({10'000, 1000, 5000, 0});
    scheduler.onFailure(0);
    TEST_ASSERT_EQUAL_UINT32(1000, scheduler.currentWaitMs());
    scheduler.onFailure(0);
    TEST_ASSERT_EQUAL_UINT32(2000, scheduler.currentWaitMs());
    scheduler.onFailure(0);
    TEST_ASSERT_EQUAL_UINT32(4000, scheduler.currentWaitMs());
    scheduler.onFailure(0);
    TEST_ASSERT_EQUAL_UINT32(5000, scheduler.currentWaitMs()); // capped
    TEST_ASSERT_FALSE(scheduler.canAttempt(4999));
    TEST_ASSERT_TRUE(scheduler.canAttempt(5000));
    TEST_ASSERT_FALSE(scheduler.needsReset()); // threshold 0 = never
}

void scheduler_requests_reset_after_failure_run_and_success_clears_it()
{
    UplinkScheduler scheduler({10'000, 1000, 60'000, 3});
    scheduler.onFailure(0);
    scheduler.onFailure(0);
    TEST_ASSERT_FALSE(scheduler.needsReset());
    scheduler.onFailure(0);
    TEST_ASSERT_TRUE(scheduler.needsReset());
    scheduler.onReset();
    TEST_ASSERT_FALSE(scheduler.needsReset());
    TEST_ASSERT_EQUAL_UINT32(4000, scheduler.currentWaitMs()); // back-off kept
    scheduler.onSuccess(100'000);
    TEST_ASSERT_EQUAL_UINT32(10'000, scheduler.currentWaitMs());
    TEST_ASSERT_EQUAL_UINT8(0, scheduler.consecutiveFailures());
}

// --- ButtonDecoder -------------------------------------------------------

void button_short_and_long_press_by_duration()
{
    ButtonDecoder decoder(20'000, 1'000'000);
    TEST_ASSERT_EQUAL(ButtonPress::None, decoder.feed(true, 0));
    TEST_ASSERT_EQUAL(ButtonPress::Short, decoder.feed(false, 300'000));
    TEST_ASSERT_EQUAL(ButtonPress::None, decoder.feed(true, 2'000'000));
    TEST_ASSERT_EQUAL(ButtonPress::Long, decoder.feed(false, 3'100'000));
}

void button_bounce_edges_inside_debounce_window_are_ignored()
{
    ButtonDecoder decoder(20'000, 1'000'000);
    decoder.feed(true, 0);
    TEST_ASSERT_EQUAL(ButtonPress::None, decoder.feed(false, 500));   // bounce
    TEST_ASSERT_EQUAL(ButtonPress::None, decoder.feed(true, 900));    // bounce
    TEST_ASSERT_TRUE(decoder.isPressed());
    TEST_ASSERT_EQUAL(ButtonPress::Short, decoder.feed(false, 200'000));
}

void button_repeated_level_after_dropped_edge_is_ignored()
{
    ButtonDecoder decoder(20'000, 1'000'000);
    decoder.feed(true, 0);
    TEST_ASSERT_EQUAL(ButtonPress::None, decoder.feed(true, 500'000)); // release edge was lost
    TEST_ASSERT_EQUAL(ButtonPress::Short, decoder.feed(false, 600'000));
}

// --- CommandParser -------------------------------------------------------

#include "logic/CommandParser.hpp"

void command_parser_recognises_words_and_set_arguments()
{
    char line1[] = "  show \r\n";
    TEST_ASSERT_EQUAL(CommandParser::Command::Show, CommandParser::parse(line1).command);
    char line2[] = "set moving_ms 2000";
    CommandParser::Parsed set = CommandParser::parse(line2);
    TEST_ASSERT_EQUAL(CommandParser::Command::Set, set.command);
    TEST_ASSERT_EQUAL_STRING("moving_ms", set.key);
    TEST_ASSERT_EQUAL_STRING("2000", set.value);
    char line3[] = "";
    TEST_ASSERT_EQUAL(CommandParser::Command::None, CommandParser::parse(line3).command);
    char line4[] = "set onlykey";
    TEST_ASSERT_EQUAL(CommandParser::Command::Unknown, CommandParser::parse(line4).command);
    char line7[] = "at AT+SAPBR=3,1,\"APN\",\"x y\"  \r";
    CommandParser::Parsed at = CommandParser::parse(line7);
    TEST_ASSERT_EQUAL(CommandParser::Command::At, at.command);
    TEST_ASSERT_EQUAL_STRING("AT+SAPBR=3,1,\"APN\",\"x y\"", at.value);
    char line8[] = "at ";
    TEST_ASSERT_EQUAL(CommandParser::Command::Unknown, CommandParser::parse(line8).command);
    char line6[] = "mount";
    TEST_ASSERT_EQUAL(CommandParser::Command::Mount, CommandParser::parse(line6).command);
    char line5[] = "frobnicate";
    TEST_ASSERT_EQUAL(CommandParser::Command::Unknown, CommandParser::parse(line5).command);
}

void command_parser_unsigned_rejects_garbage_and_overflow()
{
    uint32_t value = 0;
    TEST_ASSERT_TRUE(CommandParser::parseUnsigned("4294967295", value));
    TEST_ASSERT_EQUAL_UINT32(4294967295u, value);
    TEST_ASSERT_FALSE(CommandParser::parseUnsigned("4294967296", value));
    TEST_ASSERT_FALSE(CommandParser::parseUnsigned("12a", value));
    TEST_ASSERT_FALSE(CommandParser::parseUnsigned("", value));
    TEST_ASSERT_FALSE(CommandParser::parseUnsigned("-1", value));
}
