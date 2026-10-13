// Transport policies on the host: TrackLogger retry/recovery with a fake
// storage, OsmAnd URL building, UplinkScheduler back-off, ButtonDecoder.
#include <string.h>

#include <unity.h>

#include "logic/ButtonDecoder.hpp"
#include "transport/HealthReport.hpp"
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

void osmand_widest_settings_fit_the_url_buffer()
{
    // 95-character server URL and 23-character id (the Settings capacities
    // minus NUL) with every numeric field at its widest.
    char server[96];
    memset(server, 'x', 95);
    memcpy(server, "http://", 7);
    server[95] = '\0';
    char id[24];
    memset(id, 'y', 23);
    id[23] = '\0';
    TrackRecord record = sampleRecord();
    record.fix.latitudeE7 = -900'000'000;
    record.fix.longitudeE7 = -1'800'000'000;
    record.fix.altitudeCm = -100'000'000;
    record.fix.hdopCenti = 65'535;
    record.fix.courseDeciDeg = 3599;
    record.fix.speedCmPerS = 4'000'000;
    record.fix.utc = {2099, 12, 31, 23, 59, 59};
    record.batteryMv = 9999;
    char url[256];
    const size_t n = OsmAndProtocol::buildUrl(server, id, record, url, sizeof url);
    TEST_ASSERT_NOT_EQUAL(0, n);
    TEST_ASSERT_LESS_THAN(sizeof url, n + 1);
}

void osmand_url_too_long_returns_zero()
{
    char url[40];
    TEST_ASSERT_EQUAL_size_t(0, OsmAndProtocol::buildUrl("http://h", "d", sampleRecord(), url, sizeof url));
}

namespace
{
    HealthSnapshot sampleHealth()
    {
        HealthSnapshot h = {};
        h.uptimeS = 3725;
        h.resetCause = ResetCause::TaskWatchdog;
        h.state = TrackerState::Acquiring;
        h.fixValid = false;
        h.satellites = 3;
        h.satellitesInView = 11;
        h.satellitesTracked = 6;
        h.maxSnrDbHz = 38;
        h.quality = 0;
        h.sentencesReceived = 12345;
        h.sentencesRejected = 7;
        h.checksumFailures = 5;
        h.fieldFailures = 2;
        h.gnssSilences = 1;
        h.storageReady = true;
        h.recordsLogged = 900;
        h.recordsDroppedLogQueue = 0;
        h.storageErrors = 4;
        h.storageRemounts = 1;
        h.recordsUploaded = 450;
        h.recordsDroppedUplinkQueue = 3;
        h.uplinkFailures = 9;
        h.modemPowerCycles = 2;
        h.signalQuality = 17;
        h.uplinkBacklog = 12;
        h.displayErrors = 0;
        h.busTimeouts = 1;
        h.batteryReadErrors = 0;
        h.buttonEdgesDropped = 0;
        h.batteryMv = 3912;
        h.freeHeapBytes = 210000;
        h.minFreeHeapBytes = 180000;
        const uint32_t stacks[HealthSnapshot::taskCount] = {1200, 900, 2100, 2400, 1500};
        for (size_t i = 0; i < HealthSnapshot::taskCount; ++i)
        {
            h.stackFreeBytes[i] = stacks[i];
        }
        return h;
    }
}

void health_url_carries_every_counter()
{
    char url[512];
    const size_t n = HealthReport::buildUrl("http://demo.traccar.org:5055", "trk-1", "0.2.0",
                                            sampleHealth(), url, sizeof url);
    TEST_ASSERT_EQUAL_STRING(
        "http://demo.traccar.org:5055/?id=trk-1&health=1&fw=0.2.0&up=3725&rst=taskwdt&st=acquiring"
        "&fix=0&sat=3&sky=11&sig=6&cn0=38&q=0"
        "&nrx=12345&nrej=7&ncks=5&nfld=2&sil=1"
        "&sd=1&lok=900&ldrop=0&lerr=4&lrm=1"
        "&uok=450&udrop=3&ufail=9&ucyc=2&uq=12&csq=17"
        "&derr=0&bto=1&berr=0&bdrop=0"
        "&bmv=3912&heap=210000&hmin=180000&stk=1200/900/2100/2400/1500",
        url);
    TEST_ASSERT_EQUAL_size_t(strlen(url), n);
}

void health_url_names_the_task_behind_a_watchdog_reset()
{
    HealthSnapshot h = sampleHealth();
    strlcpy(h.resetDetail, "logger", sizeof h.resetDetail);
    char url[512];
    HealthReport::buildUrl("http://h", "d", "1", h, url, sizeof url);
    TEST_ASSERT_NOT_NULL(strstr(url, "&rst=taskwdt:logger&st=acquiring"));

    h.resetCause = ResetCause::DeepSleep;
    h.resetDetail[0] = '\0';
    HealthReport::buildUrl("http://h", "d", "1", h, url, sizeof url);
    TEST_ASSERT_NOT_NULL(strstr(url, "&rst=deepsleep&st=acquiring"));
}

void health_widest_settings_fit_the_url_buffer()
{
    // The longest server URL and device id the settings allow, with every
    // counter at seven digits (a week of NMEA at 3 sentences a second is
    // under two million) must still fit the buffer the uplink task uses
    // (Config::gsmUrlBytes = 640).
    char serverUrl[96];
    memset(serverUrl, 'u', sizeof serverUrl - 1);
    serverUrl[sizeof serverUrl - 1] = '\0';
    char deviceId[24];
    memset(deviceId, 'd', sizeof deviceId - 1);
    deviceId[sizeof deviceId - 1] = '\0';

    HealthSnapshot h = sampleHealth();
    h.uptimeS = h.sentencesReceived = h.sentencesRejected = h.checksumFailures = h.fieldFailures =
        h.gnssSilences = h.recordsLogged = h.recordsDroppedLogQueue = h.storageErrors =
            h.storageRemounts = h.recordsUploaded = h.recordsDroppedUplinkQueue = h.uplinkFailures =
                h.modemPowerCycles = h.displayErrors = h.busTimeouts = h.batteryReadErrors =
                    h.buttonEdgesDropped = h.freeHeapBytes = h.minFreeHeapBytes = 9'999'999;
    for (size_t i = 0; i < HealthSnapshot::taskCount; ++i)
    {
        h.stackFreeBytes[i] = 9'999'999;
    }
    h.resetCause = ResetCause::TaskWatchdog; // with the detail: the longest rst
    memset(h.resetDetail, 'w', sizeof h.resetDetail - 1);
    h.resetDetail[sizeof h.resetDetail - 1] = '\0';
    h.state = TrackerState::Stationary;
    h.satellites = h.satellitesInView = h.satellitesTracked = h.maxSnrDbHz = 255;
    h.quality = 9;
    h.batteryMv = 65535;
    h.signalQuality = 99;
    h.uplinkBacklog = 255;

    char url[640];
    const size_t n = HealthReport::buildUrl(serverUrl, deviceId, "10.20.30", h, url, sizeof url);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(n < sizeof url);
}

void health_url_too_long_returns_zero()
{
    char url[64];
    TEST_ASSERT_EQUAL_size_t(0, HealthReport::buildUrl("http://h", "d", "1", sampleHealth(), url, sizeof url));
}

void health_tokens_name_every_reset_cause_and_state()
{
    TEST_ASSERT_EQUAL_STRING("poweron", HealthReport::resetCauseToken(ResetCause::PowerOn));
    TEST_ASSERT_EQUAL_STRING("brownout", HealthReport::resetCauseToken(ResetCause::Brownout));
    TEST_ASSERT_EQUAL_STRING("other", HealthReport::resetCauseToken(ResetCause::Other));
    TEST_ASSERT_EQUAL_STRING("tracking", HealthReport::stateToken(TrackerState::Tracking));
    TEST_ASSERT_EQUAL_STRING("stationary", HealthReport::stateToken(TrackerState::Stationary));
    TEST_ASSERT_EQUAL_STRING("nosignal", HealthReport::stateToken(TrackerState::NoSignal));
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

void scheduler_rejection_backs_off_but_never_requests_reset()
{
    UplinkScheduler scheduler({10'000, 1000, 60'000, 3});
    for (int i = 0; i < 10; ++i)
    {
        scheduler.onRejected(0);
    }
    TEST_ASSERT_FALSE(scheduler.needsReset());
    TEST_ASSERT_EQUAL_UINT32(60'000, scheduler.currentWaitMs()); // capped back-off
    TEST_ASSERT_FALSE(scheduler.canAttempt(59'999));
    TEST_ASSERT_TRUE(scheduler.canAttempt(60'000));
    // Rejections and link failures share one back-off; only the latter count.
    scheduler.onFailure(60'000);
    TEST_ASSERT_EQUAL_UINT8(1, scheduler.consecutiveFailures());
    scheduler.onSuccess(200'000);
    TEST_ASSERT_EQUAL_UINT32(10'000, scheduler.currentWaitMs());
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

void button_press_counts_once_the_level_has_held()
{
    ButtonDecoder decoder(50'000, 1'000'000);
    TEST_ASSERT_EQUAL(ButtonPress::None, decoder.feed(true, 0));
    TEST_ASSERT_TRUE(decoder.active());
    TEST_ASSERT_EQUAL(ButtonPress::None, decoder.feed(true, 25'000));
    TEST_ASSERT_EQUAL(ButtonPress::Short, decoder.feed(true, 50'000));
    // Held on: nothing more until the long-press threshold.
    TEST_ASSERT_EQUAL(ButtonPress::None, decoder.feed(true, 300'000));
    // Released and confirmed: back to idle, no second event.
    TEST_ASSERT_EQUAL(ButtonPress::None, decoder.feed(false, 400'000));
    TEST_ASSERT_EQUAL(ButtonPress::None, decoder.feed(false, 450'000));
    TEST_ASSERT_FALSE(decoder.active());
}

void button_bounce_shorter_than_the_stable_window_is_ignored()
{
    ButtonDecoder decoder(50'000, 1'000'000);
    decoder.feed(true, 0);
    TEST_ASSERT_EQUAL(ButtonPress::None, decoder.feed(false, 5'000)); // bounce: back to idle
    TEST_ASSERT_FALSE(decoder.active());
    decoder.feed(true, 10'000);
    TEST_ASSERT_EQUAL(ButtonPress::None, decoder.feed(true, 40'000));
    TEST_ASSERT_EQUAL(ButtonPress::Short, decoder.feed(true, 60'000));
    // Bounce on the release: the press is already counted, so a dip and a
    // return to "pressed" change nothing.
    TEST_ASSERT_EQUAL(ButtonPress::None, decoder.feed(false, 200'000));
    TEST_ASSERT_EQUAL(ButtonPress::None, decoder.feed(true, 210'000));
    TEST_ASSERT_EQUAL(ButtonPress::None, decoder.feed(false, 220'000));
    TEST_ASSERT_EQUAL(ButtonPress::None, decoder.feed(false, 270'000));
    TEST_ASSERT_FALSE(decoder.active());
}

void button_long_press_is_reported_once_while_held()
{
    ButtonDecoder decoder(50'000, 1'000'000);
    decoder.feed(true, 0);
    TEST_ASSERT_EQUAL(ButtonPress::Short, decoder.feed(true, 50'000));
    TEST_ASSERT_EQUAL(ButtonPress::None, decoder.feed(true, 900'000));
    TEST_ASSERT_EQUAL(ButtonPress::Long, decoder.feed(true, 1'000'000));
    TEST_ASSERT_EQUAL(ButtonPress::None, decoder.feed(true, 3'000'000));
    TEST_ASSERT_EQUAL(ButtonPress::None, decoder.feed(false, 3'100'000));
    TEST_ASSERT_EQUAL(ButtonPress::None, decoder.feed(false, 3'150'000));
    TEST_ASSERT_FALSE(decoder.active());
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
