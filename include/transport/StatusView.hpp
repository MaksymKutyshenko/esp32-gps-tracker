#pragma once

#include <stdint.h>

#include "esp_err.h"

#include "logic/Fix.hpp"
#include "logic/TrackerFsm.hpp"
#include "periph/Ssd1306.hpp"

// Everything the screen can show, merged by the display task from
// DisplayMessages. Plain data so the view is a pure function of it.
struct StatusSnapshot
{
    TrackerState state;
    Fix fix;
    bool haveFix;
    uint16_t batteryMv;
    bool storageReady;
    uint32_t recordsWritten;
    uint32_t recordsLost;
    bool uplinkConnected;
    uint32_t recordsUploaded;
    uint32_t uplinkFailures;
    uint8_t signalQuality;
};

// Composes the OLED frame for the current page. Page 0 is the tracking
// page (state, satellites, speed, position); page 1 is the system page
// (battery, storage, uplink). The button cycles pages.
class StatusView
{
public:
    static constexpr uint8_t pageCount = 2;

    explicit StatusView(Ssd1306 &display);

    esp_err_t render(const StatusSnapshot &snapshot, uint8_t page);

private:
    void drawTrackingPage(const StatusSnapshot &snapshot);
    void drawSystemPage(const StatusSnapshot &snapshot);

    Ssd1306 &display_;
};
