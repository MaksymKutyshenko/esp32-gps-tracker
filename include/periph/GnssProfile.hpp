#pragma once

#include <stddef.h>
#include <stdint.h>

// What differs between the GNSS modules this tracker can be built with.
// A Strategy chosen once at boot from Settings::gnssProfile; the receiver
// task never asks which module it is talking to.
struct GnssProfile
{
    const char *name;
    uint32_t baudRate;
};

namespace GnssProfiles
{
    // u-blox M10 class, 10 Hz, NMEA at 115200 by default.
    constexpr GnssProfile foxeerM10q250 = {"Foxeer M10Q-250", 115200};
    // u-blox M8 class, 1 Hz, NMEA at 9600 by default.
    constexpr GnssProfile beitianBe220 = {"Beitian BE-220", 9600};

    constexpr const GnssProfile *all[] = {&foxeerM10q250, &beitianBe220};
    constexpr size_t count = sizeof all / sizeof all[0];

    inline const GnssProfile &byIndex(uint8_t index)
    {
        return index < count ? *all[index] : *all[0];
    }
}
