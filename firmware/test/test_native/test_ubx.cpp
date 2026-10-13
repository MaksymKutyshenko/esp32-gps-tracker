// UbxFrame: the receiver configuration frames, checked against bytes
// computed independently (Python, Fletcher-8 over class..payload).
#include <unity.h>

#include "logic/UbxAck.hpp"
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
    TEST_ASSERT_EQUAL_size_t(3, GnssProfiles::foxeerM10q250.setupCount);
    TEST_ASSERT_EQUAL_size_t(5, GnssProfiles::beitianBe220.setupCount);
}

void ubx_static_hold_frames_carry_threshold_and_checksum()
{
    const UbxCommand &m10 = GnssProfiles::m10Commands[1];
    TEST_ASSERT_EQUAL_size_t(23, m10.length);
    TEST_ASSERT_EQUAL_UINT8(100, m10.bytes[14]); // speed threshold value
    TEST_ASSERT_EQUAL_UINT8(20, m10.bytes[19]);  // distance threshold low byte
    TEST_ASSERT_EQUAL_UINT8(0x25, m10.bytes[21]);
    TEST_ASSERT_EQUAL_UINT8(0x52, m10.bytes[22]);

    const UbxCommand &m8 = GnssProfiles::m8Commands[4];
    TEST_ASSERT_EQUAL_size_t(44, m8.length);
    TEST_ASSERT_EQUAL_UINT8(0x24, m8.bytes[3]);  // CFG-NAV5
    TEST_ASSERT_EQUAL_UINT8(0x50, m8.bytes[6]);  // posMask | staticHold
    TEST_ASSERT_EQUAL_UINT8(30, m8.bytes[6 + 18]); // pAcc
    TEST_ASSERT_EQUAL_UINT8(100, m8.bytes[6 + 22]);
    TEST_ASSERT_EQUAL_UINT8(20, m8.bytes[6 + 28]);
    TEST_ASSERT_EQUAL_UINT8(0x34, m8.bytes[42]);
    TEST_ASSERT_EQUAL_UINT8(0x38, m8.bytes[43]);

    const UbxCommand &acc = GnssProfiles::m10Commands[2];
    TEST_ASSERT_EQUAL_size_t(18, acc.length);
    TEST_ASSERT_EQUAL_UINT8(30, acc.bytes[14]); // accuracy mask, metres
    TEST_ASSERT_EQUAL_UINT8(0x11, acc.bytes[16]);
    TEST_ASSERT_EQUAL_UINT8(0x71, acc.bytes[17]);
}

namespace
{
    UbxAckScanner::Result feed(UbxAckScanner &scanner, const uint8_t *bytes, size_t length,
                               uint8_t cls, uint8_t id)
    {
        UbxAckScanner::Result last = UbxAckScanner::Result::None;
        for (size_t i = 0; i < length; ++i)
        {
            const UbxAckScanner::Result r = scanner.push(bytes[i], cls, id);
            if (r != UbxAckScanner::Result::None)
            {
                last = r;
            }
        }
        return last;
    }
}

void ubx_ack_scanner_finds_ack_and_nak_inside_nmea_text()
{
    // "$GNRMC,...*7A\r\n" then ACK-ACK for CFG-VALSET (0x06 0x8A), more text.
    const uint8_t ackStream[] = {'$', 'G', 'N', 'R', 'M', 'C', ',', '*', '7', 'A', '\r', '\n',
                                 0xB5, 0x62, 0x05, 0x01, 0x02, 0x00, 0x06, 0x8A, 0x98, 0xC1,
                                 '$', 'G', 'N'};
    UbxAckScanner scanner;
    TEST_ASSERT_EQUAL(UbxAckScanner::Result::Ack, feed(scanner, ackStream, sizeof ackStream, 0x06, 0x8A));

    const uint8_t nakStream[] = {0xB5, 0x62, 0x05, 0x00, 0x02, 0x00, 0x06, 0x8A, 0x97, 0xBC};
    UbxAckScanner scanner2;
    TEST_ASSERT_EQUAL(UbxAckScanner::Result::Nak, feed(scanner2, nakStream, sizeof nakStream, 0x06, 0x8A));
}

void ubx_ack_scanner_ignores_other_frames_and_bad_checksums()
{
    const uint8_t ackForOther[] = {0xB5, 0x62, 0x05, 0x01, 0x02, 0x00, 0x06, 0x8A, 0x98, 0xC1};
    UbxAckScanner scanner;
    TEST_ASSERT_EQUAL(UbxAckScanner::Result::None, feed(scanner, ackForOther, sizeof ackForOther, 0x06, 0x24));

    const uint8_t corrupted[] = {0xB5, 0x62, 0x05, 0x01, 0x02, 0x00, 0x06, 0x8A, 0x98, 0xC0};
    UbxAckScanner scanner2;
    TEST_ASSERT_EQUAL(UbxAckScanner::Result::None, feed(scanner2, corrupted, sizeof corrupted, 0x06, 0x8A));
}
