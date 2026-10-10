// UbxFrame: the receiver configuration frames, checked against bytes
// computed independently (Python, Fletcher-8 over class..payload).
#include <unity.h>

#include "logic/UbxFrame.hpp"
#include "periph/GnssProfile.hpp"

void ubx_frame_matches_reference_cfg_rate()
{
    // UBX-CFG-RATE 1000 ms / 1 / GPS: the well-known 1 Hz frame.
    constexpr UbxFrame<6> frame(0x06, 0x08, {0xE8, 0x03, 0x01, 0x00, 0x01, 0x00});
    const uint8_t expected[] = {0xB5, 0x62, 0x06, 0x08, 0x06, 0x00,
                                0xE8, 0x03, 0x01, 0x00, 0x01, 0x00, 0x01, 0x39};
    TEST_ASSERT_EQUAL_size_t(sizeof expected, frame.length());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frame.data(), sizeof expected);
}

void ubx_frame_matches_reference_cfg_msg()
{
    constexpr UbxFrame<3> frame(0x06, 0x01, {0xF0, 0x01, 0x00});
    const uint8_t expected[] = {0xB5, 0x62, 0x06, 0x01, 0x03, 0x00, 0xF0, 0x01, 0x00, 0xFB, 0x11};
    TEST_ASSERT_EQUAL_size_t(sizeof expected, frame.length());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frame.data(), sizeof expected);
}

void ubx_m10_setup_frame_has_valset_header_and_checksum()
{
    const UbxCommand &cmd = GnssProfiles::m10Commands[0];
    TEST_ASSERT_EQUAL_size_t(48, cmd.length);
    TEST_ASSERT_EQUAL_UINT8(0x06, cmd.bytes[2]); // CFG
    TEST_ASSERT_EQUAL_UINT8(0x8A, cmd.bytes[3]); // VALSET
    TEST_ASSERT_EQUAL_UINT8(40, cmd.bytes[4]);
    TEST_ASSERT_EQUAL_UINT8(0x01, cmd.bytes[7]); // RAM layer only
    TEST_ASSERT_EQUAL_UINT8(0x86, cmd.bytes[46]);
    TEST_ASSERT_EQUAL_UINT8(0xCF, cmd.bytes[47]);
    TEST_ASSERT_EQUAL_size_t(1, GnssProfiles::foxeerM10q250.setupCount);
    TEST_ASSERT_EQUAL_size_t(4, GnssProfiles::beitianBe220.setupCount);
}
