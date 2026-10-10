#include "transport/StatusView.hpp"

#include <stdio.h>

#include "logic/TrackRecord.hpp"

namespace
{
    constexpr int32_t lineHeight = 10;

    const char *stateLabel(TrackerState state)
    {
        switch (state)
        {
        case TrackerState::Booting: return "BOOT";
        case TrackerState::NoSignal: return "NO GPS";
        case TrackerState::Acquiring: return "SEARCH";
        case TrackerState::Tracking: return "TRACK";
        case TrackerState::Stationary: return "PARKED";
        case TrackerState::Sleeping: return "SLEEP";
        }
        return "?";
    }
}

StatusView::StatusView(Ssd1306 &display) : display_(display)
{
}

esp_err_t StatusView::render(const StatusSnapshot &snapshot, uint8_t page)
{
    display_.clear();
    if (page % pageCount == 0)
    {
        drawTrackingPage(snapshot);
    }
    else
    {
        drawSystemPage(snapshot);
    }
    return display_.flush();
}

void StatusView::drawTrackingPage(const StatusSnapshot &snapshot)
{
    char text[32];

    // Line 1: state bold, satellites right.
    display_.drawText(0, 0, stateLabel(snapshot.state), 2);
    // used / with signal: "0/6" means satellites are being received but
    // not yet decoded, "0/0" means the antenna delivers nothing. The
    // almanac's own count is on the system page.
    snprintf(text, sizeof text, "%u/%u sat", snapshot.fix.satellites, snapshot.fix.satellitesTracked);
    display_.drawText(Ssd1306::width - Ssd1306::textWidth(text, 1), 4, text, 1);

    // Line 2: time (UTC) and speed.
    if (snapshot.haveFix || snapshot.fix.utc.year != 0)
    {
        snprintf(text, sizeof text, "%02u:%02u:%02uZ", snapshot.fix.utc.hour,
                 snapshot.fix.utc.minute, snapshot.fix.utc.second);
        display_.drawText(0, 20, text, 1);
    }
    char speed[12];
    TrackRecordFormat::formatFixed(speed, sizeof speed,
                                   static_cast<int64_t>(snapshot.fix.speedCmPerS) * 36 / 100, 1);
    snprintf(text, sizeof text, "%s km/h", snapshot.haveFix ? speed : "--.-");
    display_.drawText(Ssd1306::width - Ssd1306::textWidth(text, 1), 20, text, 1);

    // Lines 3-4: position, or dashes until the first fix.
    if (snapshot.haveFix)
    {
        char value[16];
        TrackRecordFormat::formatFixed(value, sizeof value, snapshot.fix.latitudeE7, 7);
        snprintf(text, sizeof text, "LAT %s", value);
        display_.drawText(0, 34, text, 1);
        TrackRecordFormat::formatFixed(value, sizeof value, snapshot.fix.longitudeE7, 7);
        snprintf(text, sizeof text, "LON %s", value);
        display_.drawText(0, 34 + lineHeight, text, 1);
    }
    else
    {
        display_.drawText(0, 34, "LAT --.-------", 1);
        display_.drawText(0, 34 + lineHeight, "LON --.-------", 1);
    }

    // Line 5: compact health strip.
    snprintf(text, sizeof text, "%u.%02uV SD:%s UP:%s",
             snapshot.batteryMv / 1000, (snapshot.batteryMv % 1000) / 10,
             snapshot.storageReady ? "ok" : "--",
             snapshot.uplinkConnected ? "ok" : "--");
    display_.drawText(0, 56, text, 1);
}

void StatusView::drawSystemPage(const StatusSnapshot &snapshot)
{
    char text[32];
    display_.drawText(0, 0, "SYSTEM", 2);

    snprintf(text, sizeof text, "Batt %u.%02u V", snapshot.batteryMv / 1000,
             (snapshot.batteryMv % 1000) / 10);
    display_.drawText(0, 20, text, 1);

    snprintf(text, sizeof text, "SD %s  w:%lu l:%lu",
             snapshot.storageReady ? "ready" : "none",
             static_cast<unsigned long>(snapshot.recordsWritten),
             static_cast<unsigned long>(snapshot.recordsLost));
    display_.drawText(0, 20 + lineHeight, text, 1);

    snprintf(text, sizeof text, "GSM %s  csq:%u",
             snapshot.uplinkConnected ? "up" : "down",
             snapshot.signalQuality);
    display_.drawText(0, 20 + 2 * lineHeight, text, 1);

    snprintf(text, sizeof text, "sent:%lu fail:%lu",
             static_cast<unsigned long>(snapshot.recordsUploaded),
             static_cast<unsigned long>(snapshot.uplinkFailures));
    display_.drawText(0, 20 + 3 * lineHeight, text, 1);

    snprintf(text, sizeof text, "HDOP %u.%02u Q%u", snapshot.fix.hdopCenti / 100,
             snapshot.fix.hdopCenti % 100, snapshot.fix.quality);
    display_.drawText(0, 20 + 4 * lineHeight, text, 1);

    // Sky: satellites the almanac puts above the horizon; sig: those with
    // a measured C/N0; cn0: the strongest one in dB-Hz (open sky 35-50).
    snprintf(text, sizeof text, "sky %u sig %u cn0 %u", snapshot.fix.satellitesInView,
             snapshot.fix.satellitesTracked, snapshot.fix.maxSnrDbHz);
    display_.drawText(0, 56, text, 1);
}
