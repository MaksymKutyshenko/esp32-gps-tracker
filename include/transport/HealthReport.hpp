#pragma once

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "logic/TrackerFsm.hpp"

// Why the chip last started. Mirrors esp_reset_reason_t without the ESP-IDF
// header so the report builder stays host-testable; App maps one to the
// other once at boot.
enum class ResetCause : uint8_t
{
    PowerOn,
    Software,
    Panic,
    InterruptWatchdog,
    TaskWatchdog,
    OtherWatchdog,
    DeepSleep,
    Brownout,
    Other,
};

// Everything the tracker knows about its own condition at one instant: the
// Diagnostics counters, the receiver's sky view, the card and modem state,
// battery, heap and stack headroom. The ui task fills it once every
// battery tick and overwrites a single-slot queue; the uplink task sends
// the latest one to the server every Settings::healthIntervalMs as a
// "health" report, so the web app can tell which module is in trouble
// even while there is no fix and no position to report.
struct HealthSnapshot
{
    uint32_t uptimeS;
    ResetCause resetCause;
    TrackerState state;

    // GNSS
    bool fixValid;
    uint8_t satellites;
    uint8_t satellitesInView;
    uint8_t satellitesTracked;
    uint8_t maxSnrDbHz;
    uint8_t quality;
    uint32_t sentencesReceived;
    uint32_t sentencesRejected;
    uint32_t checksumFailures;
    uint32_t fieldFailures;
    uint32_t gnssSilences;

    // Storage
    bool storageReady;
    uint32_t recordsLogged;
    uint32_t recordsDroppedLogQueue;
    uint32_t storageErrors;
    uint32_t storageRemounts;

    // Uplink (signalQuality and backlog are patched in by the uplink task,
    // the only one that may talk to the modem)
    uint32_t recordsUploaded;
    uint32_t recordsDroppedUplinkQueue;
    uint32_t uplinkFailures;
    uint32_t modemPowerCycles;
    uint8_t signalQuality; // 0..31, 99 unknown
    uint8_t uplinkBacklog; // records waiting in the uplink queue

    // UI
    uint32_t displayErrors;
    uint32_t busTimeouts;
    uint32_t batteryReadErrors;
    uint32_t buttonEdgesDropped;

    // Power and memory
    uint16_t batteryMv;
    uint32_t freeHeapBytes;
    uint32_t minFreeHeapBytes;
    static constexpr size_t taskCount = 5; // gps, display, logger, uplink, ui
    uint32_t stackFreeBytes[taskCount];
};

// Health report in the same shape as the position report: one HTTP GET
// with the values in the query string, marked by health=1 so a server that
// only speaks OsmAnd ignores it (Traccar stores unknown keys as
// attributes) and the tracker's own web app files it as a health row.
// Keys are short because every byte goes over 2G.
//
// Pure logic, no ESP-IDF dependency; unit-tested on the host.
namespace HealthReport
{
    inline const char *resetCauseToken(ResetCause cause)
    {
        switch (cause)
        {
        case ResetCause::PowerOn: return "poweron";
        case ResetCause::Software: return "sw";
        case ResetCause::Panic: return "panic";
        case ResetCause::InterruptWatchdog: return "intwdt";
        case ResetCause::TaskWatchdog: return "taskwdt";
        case ResetCause::OtherWatchdog: return "wdt";
        case ResetCause::DeepSleep: return "deepsleep";
        case ResetCause::Brownout: return "brownout";
        default: return "other";
        }
    }

    inline const char *stateToken(TrackerState state)
    {
        switch (state)
        {
        case TrackerState::Booting: return "boot";
        case TrackerState::NoSignal: return "nosignal";
        case TrackerState::Acquiring: return "acquiring";
        case TrackerState::Tracking: return "tracking";
        case TrackerState::Stationary: return "stationary";
        case TrackerState::Sleeping: return "sleeping";
        default: return "unknown";
        }
    }

    // Returns the URL length, or 0 if it did not fit.
    inline size_t buildUrl(const char *serverUrl, const char *deviceId, const char *firmware,
                           const HealthSnapshot &s, char *out, size_t capacity)
    {
        const int n = snprintf(
            out, capacity,
            "%s/?id=%s&health=1&fw=%s&up=%" PRIu32 "&rst=%s&st=%s"
            "&fix=%u&sat=%u&sky=%u&sig=%u&cn0=%u&q=%u"
            "&nrx=%" PRIu32 "&nrej=%" PRIu32 "&ncks=%" PRIu32 "&nfld=%" PRIu32 "&sil=%" PRIu32
            "&sd=%u&lok=%" PRIu32 "&ldrop=%" PRIu32 "&lerr=%" PRIu32 "&lrm=%" PRIu32
            "&uok=%" PRIu32 "&udrop=%" PRIu32 "&ufail=%" PRIu32 "&ucyc=%" PRIu32 "&uq=%u&csq=%u"
            "&derr=%" PRIu32 "&bto=%" PRIu32 "&berr=%" PRIu32 "&bdrop=%" PRIu32
            "&bmv=%u&heap=%" PRIu32 "&hmin=%" PRIu32
            "&stk=%" PRIu32 "/%" PRIu32 "/%" PRIu32 "/%" PRIu32 "/%" PRIu32,
            serverUrl, deviceId, firmware, s.uptimeS, resetCauseToken(s.resetCause), stateToken(s.state),
            s.fixValid ? 1u : 0u, s.satellites, s.satellitesInView, s.satellitesTracked, s.maxSnrDbHz, s.quality,
            s.sentencesReceived, s.sentencesRejected, s.checksumFailures, s.fieldFailures, s.gnssSilences,
            s.storageReady ? 1u : 0u, s.recordsLogged, s.recordsDroppedLogQueue, s.storageErrors, s.storageRemounts,
            s.recordsUploaded, s.recordsDroppedUplinkQueue, s.uplinkFailures, s.modemPowerCycles,
            s.uplinkBacklog, s.signalQuality,
            s.displayErrors, s.busTimeouts, s.batteryReadErrors, s.buttonEdgesDropped,
            s.batteryMv, s.freeHeapBytes, s.minFreeHeapBytes,
            s.stackFreeBytes[0], s.stackFreeBytes[1], s.stackFreeBytes[2], s.stackFreeBytes[3],
            s.stackFreeBytes[4]);
        return (n > 0 && static_cast<size_t>(n) < capacity) ? static_cast<size_t>(n) : 0;
    }
}
