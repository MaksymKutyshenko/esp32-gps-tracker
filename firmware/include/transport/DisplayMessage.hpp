#pragma once

#include <stdint.h>

#include "logic/Fix.hpp"
#include "logic/TrackerFsm.hpp"

// What the other tasks tell the display task. One tagged union through one
// queue: each producer sends only the fields it owns, and the display task
// merges them into its StatusSnapshot. Producers never touch display state.
struct DisplayMessage
{
    enum class Kind : uint8_t
    {
        FixUpdate,      // gps task: state + latest fix (display-filtered speed/altitude)
        Battery,        // ui task
        StorageHealth,  // logger task
        UplinkHealth,   // uplink task
        NextPage,       // ui task: button press
    };

    Kind kind;
    union
    {
        struct
        {
            TrackerState state;
            Fix fix;
        } fixUpdate;
        struct
        {
            uint16_t millivolts;
        } battery;
        struct
        {
            bool ready;
            uint32_t recordsWritten;
            uint32_t recordsLost;
        } storage;
        struct
        {
            bool connected;
            uint32_t recordsUploaded;
            uint32_t failures;
            uint8_t signalQuality;
        } uplink;
    };
};
