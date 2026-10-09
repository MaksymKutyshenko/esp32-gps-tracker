#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

// Field-adjustable parameters persisted in NVS. Everything a user might
// tune without a rebuild lives here: logging cadence, motion thresholds,
// the GNSS module in use, the APN and the upload target.
//
// Values are stored as one versioned blob. On load every field is range
// checked and anything out of range falls back to the Config default, so a
// corrupt or outdated blob degrades to defaults instead of to nonsense.
class Settings
{
public:
    static constexpr uint32_t layoutVersion = 1;
    static constexpr size_t apnCapacity = 32;
    static constexpr size_t serverUrlCapacity = 96;
    static constexpr size_t deviceIdCapacity = 24;

    struct Values
    {
        uint32_t version;

        // LogPolicy
        uint32_t movingIntervalMs;
        uint32_t stationaryIntervalMs;
        uint32_t minDistanceM;

        // SpeedMotionDetector
        uint32_t motionStartCmPerS;
        uint32_t motionStopCmPerS;
        uint32_t motionStopDelayMs;

        // Index into GnssProfiles::all
        uint8_t gnssProfile;

        // Uplink
        bool uplinkEnabled;
        uint32_t uplinkMinIntervalMs;
        char apn[apnCapacity];
        char serverUrl[serverUrlCapacity];
        char deviceId[deviceIdCapacity];
    };

    // Initialises NVS; erases and retries when the partition was written by
    // a newer NVS layout. Aborts on anything else: without NVS the device
    // has no identity or settings.
    void begin();

    // Loads and validates. False when nothing usable was stored; `values`
    // then holds the defaults. Out-of-range fields are replaced one by one.
    bool load(Values &values) const;

    esp_err_t save(const Values &values) const;

    static Values defaults();

    // Clamps every field into its allowed range; true if nothing changed.
    static bool validate(Values &values);
};
