#pragma once

#include <stddef.h>
#include <stdint.h>

#include "logic/UbxFrame.hpp"

// What differs between the GNSS modules this tracker can be built with.
// A Strategy chosen once at boot from Settings::gnssProfile; the receiver
// task never asks which module it is talking to.
//
// `setup` is sent once after the UART is up. Both modules ship at 10 Hz
// with GLL, VTG and GSA on, which is 120 sentences a second and most of
// the UART's capacity; a tracker that records every few seconds needs
// 1 Hz and three sentence types (RMC, GGA, GSV). The commands change the
// receiver's RAM configuration only, so the module is back to its own
// defaults after a power cycle and nothing is written to its flash.
struct GnssProfile
{
    const char *name;
    uint32_t baudRate;
    const UbxCommand *setup;
    size_t setupCount;
};

namespace GnssProfiles
{
    // ---- u-blox M10 (protocol 34+): UBX-CFG-VALSET, one frame -----------
    // Payload: version 0, layer 1 (RAM), 2 reserved bytes, then key/value
    // pairs with 32-bit little-endian keys.
    inline constexpr UbxFrame<40> m10Setup(0x06, 0x8A, {
        0x00, 0x01, 0x00, 0x00,
        0x01, 0x00, 0x21, 0x30, 0xE8, 0x03,  // CFG-RATE-MEAS         = 1000 ms
        0xCA, 0x00, 0x91, 0x20, 0x00,        // CFG-MSGOUT-NMEA_ID_GLL_UART1 = 0
        0xB1, 0x00, 0x91, 0x20, 0x00,        // CFG-MSGOUT-NMEA_ID_VTG_UART1 = 0
        0xC0, 0x00, 0x91, 0x20, 0x00,        // CFG-MSGOUT-NMEA_ID_GSA_UART1 = 0
        0xAC, 0x00, 0x91, 0x20, 0x01,        // CFG-MSGOUT-NMEA_ID_RMC_UART1 = 1
        0xBB, 0x00, 0x91, 0x20, 0x01,        // CFG-MSGOUT-NMEA_ID_GGA_UART1 = 1
        0xC5, 0x00, 0x91, 0x20, 0x01,        // CFG-MSGOUT-NMEA_ID_GSV_UART1 = 1
    });
    inline constexpr UbxCommand m10Commands[] = {
        {m10Setup.data(), m10Setup.length()},
    };

    // ---- u-blox M8 (protocol < 24): legacy UBX-CFG-RATE and UBX-CFG-MSG --
    inline constexpr UbxFrame<6> m8Rate(0x06, 0x08, {
        0xE8, 0x03,  // measRate 1000 ms
        0x01, 0x00,  // navRate: one solution per measurement
        0x01, 0x00,  // timeRef: GPS time
    });
    // CFG-MSG with a 3-byte payload sets the rate on the port it arrives on.
    inline constexpr UbxFrame<3> m8GllOff(0x06, 0x01, {0xF0, 0x01, 0x00});
    inline constexpr UbxFrame<3> m8GsaOff(0x06, 0x01, {0xF0, 0x02, 0x00});
    inline constexpr UbxFrame<3> m8VtgOff(0x06, 0x01, {0xF0, 0x05, 0x00});
    inline constexpr UbxCommand m8Commands[] = {
        {m8Rate.data(), m8Rate.length()},
        {m8GllOff.data(), m8GllOff.length()},
        {m8GsaOff.data(), m8GsaOff.length()},
        {m8VtgOff.data(), m8VtgOff.length()},
    };

    constexpr GnssProfile foxeerM10q250 = {"Foxeer M10Q-250", 115200, m10Commands,
                                           sizeof m10Commands / sizeof m10Commands[0]};
    constexpr GnssProfile beitianBe220 = {"Beitian BE-220", 9600, m8Commands,
                                          sizeof m8Commands / sizeof m8Commands[0]};

    constexpr const GnssProfile *all[] = {&foxeerM10q250, &beitianBe220};
    constexpr size_t count = sizeof all / sizeof all[0];

    inline const GnssProfile &byIndex(uint8_t index)
    {
        return index < count ? *all[index] : *all[0];
    }
}
