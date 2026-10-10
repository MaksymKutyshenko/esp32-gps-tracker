// NMEA assembler and parser: framing, checksum, field validation.
#include <string.h>

#include <unity.h>

#include "logic/NmeaLineAssembler.hpp"
#include "logic/NmeaParser.hpp"

namespace
{
    // 2026-10-09 10:30:45 UTC, 50 27.12345' N, 30 31.54321' E, 12.345 kn, 210.5 deg
    const char *rmcValid = "$GNRMC,103045.00,A,5027.12345,N,03031.54321,E,12.345,210.5,091026,,,A,V*03";
    const char *ggaValid = "$GNGGA,103045.00,5027.12345,N,03031.54321,E,1,08,0.95,179.3,M,33.1,M,,*49";
    const char *rmcNoFix = "$GNRMC,103046.00,V,,,,,,,091026,,,N,V*15";
    const char *rmcBadMinutes = "$GNRMC,103047.00,A,5067.00000,N,03031.54321,E,0.0,0.0,091026,,,A,V*33";
    const char *gsaOther = "$GNGSA,A,3,05,07,,,,,,,,,,,1.5,0.9,1.2,1*3D";
    const char *rmcEmptySpeed = "$GNRMC,103048.00,A,5027.12345,N,03031.54321,E,,,091026,,,A,V*39";
    const char *ggaNoFix = "$GNGGA,103049.00,,,,,0,00,,,M,,M,,*59";

    template <size_t Capacity>
    void feed(NmeaLineAssembler<Capacity> &assembler, const char *text, size_t &completed)
    {
        for (; *text != '\0'; ++text)
        {
            if (assembler.push(*text))
            {
                ++completed;
            }
        }
    }
}

// --- checksum ------------------------------------------------------------

void nmea_checksum_accepts_correct_sentence()
{
    TEST_ASSERT_TRUE(NmeaParser::verifyChecksum(rmcValid));
    TEST_ASSERT_TRUE(NmeaParser::verifyChecksum(ggaValid));
}

void nmea_checksum_rejects_corruption()
{
    char corrupted[96];
    strcpy(corrupted, rmcValid);
    corrupted[20] = '9'; // one digit of the latitude
    TEST_ASSERT_FALSE(NmeaParser::verifyChecksum(corrupted));
}

void nmea_checksum_rejects_missing_star_and_bad_hex()
{
    TEST_ASSERT_FALSE(NmeaParser::verifyChecksum("$GNRMC,1,2,3"));
    TEST_ASSERT_FALSE(NmeaParser::verifyChecksum("$GNRMC,1,2,3*ZZ"));
    TEST_ASSERT_FALSE(NmeaParser::verifyChecksum("$GNRMC,1,2,3*0"));
    TEST_ASSERT_FALSE(NmeaParser::verifyChecksum("GNRMC,1*00"));
    TEST_ASSERT_FALSE(NmeaParser::verifyChecksum(nullptr));
}

// --- RMC / GGA -----------------------------------------------------------

void nmea_rmc_parses_position_speed_course_and_time()
{
    NmeaParser parser;
    TEST_ASSERT_EQUAL(NmeaParser::Sentence::Rmc, parser.parse(rmcValid));

    const Fix &fix = parser.fix();
    TEST_ASSERT_TRUE(fix.valid);
    TEST_ASSERT_EQUAL_INT32(504520575, fix.latitudeE7);
    TEST_ASSERT_EQUAL_INT32(305257202, fix.longitudeE7);
    TEST_ASSERT_EQUAL_UINT32(635, fix.speedCmPerS);   // 12.345 kn = 6.35 m/s
    TEST_ASSERT_EQUAL_UINT16(2105, fix.courseDeciDeg);
    TEST_ASSERT_EQUAL_UINT16(2026, fix.utc.year);
    TEST_ASSERT_EQUAL_UINT8(10, fix.utc.month);
    TEST_ASSERT_EQUAL_UINT8(9, fix.utc.day);
    TEST_ASSERT_EQUAL_UINT8(10, fix.utc.hour);
    TEST_ASSERT_EQUAL_UINT8(30, fix.utc.minute);
    TEST_ASSERT_EQUAL_UINT8(45, fix.utc.second);
}

void nmea_gga_merges_quality_satellites_hdop_altitude()
{
    NmeaParser parser;
    parser.parse(rmcValid);
    TEST_ASSERT_EQUAL(NmeaParser::Sentence::Gga, parser.parse(ggaValid));

    const Fix &fix = parser.fix();
    TEST_ASSERT_TRUE(fix.valid); // GGA does not touch the RMC validity
    TEST_ASSERT_EQUAL_UINT8(1, fix.quality);
    TEST_ASSERT_EQUAL_UINT8(8, fix.satellites);
    TEST_ASSERT_EQUAL_UINT16(95, fix.hdopCenti);
    TEST_ASSERT_EQUAL_INT32(17930, fix.altitudeCm);
    TEST_ASSERT_EQUAL_INT32(504520575, fix.latitudeE7); // position kept
}

void nmea_rmc_without_fix_clears_valid_but_keeps_time()
{
    NmeaParser parser;
    parser.parse(rmcValid);
    TEST_ASSERT_EQUAL(NmeaParser::Sentence::Rmc, parser.parse(rmcNoFix));

    const Fix &fix = parser.fix();
    TEST_ASSERT_FALSE(fix.valid);
    TEST_ASSERT_EQUAL_UINT8(46, fix.utc.second);
    // Stale position stays in the struct; `valid` is what gates its use.
    TEST_ASSERT_EQUAL_INT32(504520575, fix.latitudeE7);
}

void nmea_rmc_rejects_minutes_over_sixty_and_keeps_previous_fix()
{
    NmeaParser parser;
    parser.parse(rmcValid);
    TEST_ASSERT_EQUAL(NmeaParser::Sentence::Invalid, parser.parse(rmcBadMinutes));
    TEST_ASSERT_EQUAL_UINT32(1, parser.fieldFailures());
    TEST_ASSERT_EQUAL_UINT8(45, parser.fix().utc.second); // untouched
}

void nmea_rmc_empty_speed_and_course_mean_zero()
{
    NmeaParser parser;
    parser.parse(rmcValid);
    TEST_ASSERT_EQUAL(NmeaParser::Sentence::Rmc, parser.parse(rmcEmptySpeed));
    TEST_ASSERT_EQUAL_UINT32(0, parser.fix().speedCmPerS);
    TEST_ASSERT_EQUAL_UINT16(0, parser.fix().courseDeciDeg);
}

void nmea_gga_without_fix_parses_zero_satellites()
{
    NmeaParser parser;
    TEST_ASSERT_EQUAL(NmeaParser::Sentence::Gga, parser.parse(ggaNoFix));
    TEST_ASSERT_EQUAL_UINT8(0, parser.fix().quality);
    TEST_ASSERT_EQUAL_UINT8(0, parser.fix().satellites);
}

void nmea_other_sentence_types_are_ignored_but_valid()
{
    NmeaParser parser;
    TEST_ASSERT_EQUAL(NmeaParser::Sentence::Other, parser.parse(gsaOther));
    TEST_ASSERT_EQUAL_UINT32(0, parser.checksumFailures());
}

void nmea_corrupted_sentence_counts_checksum_failure()
{
    NmeaParser parser;
    char corrupted[96];
    strcpy(corrupted, ggaValid);
    corrupted[30] = 'X';
    TEST_ASSERT_EQUAL(NmeaParser::Sentence::Invalid, parser.parse(corrupted));
    TEST_ASSERT_EQUAL_UINT32(1, parser.checksumFailures());
}

void nmea_western_and_southern_hemispheres_are_negative()
{
    NmeaParser parser;
    // 33 52.0000' S, 151 12.0000' W
    TEST_ASSERT_EQUAL(NmeaParser::Sentence::Rmc,
                      parser.parse("$GPRMC,000000,A,3352.0000,S,15112.0000,W,0.0,0.0,010126,,,A*7A"));
    TEST_ASSERT_EQUAL_INT32(-338666667, parser.fix().latitudeE7);
    TEST_ASSERT_EQUAL_INT32(-1512000000, parser.fix().longitudeE7);
}

void nmea_gsv_counts_satellites_in_view_per_epoch()
{
    NmeaParser parser;
    // GPS talker hears 7, GLONASS talker hears 9: the epoch reports 9.
    TEST_ASSERT_EQUAL(NmeaParser::Sentence::Gsv,
                      parser.parse("$GPGSV,2,1,07,01,40,083,41,02,17,308,43,12,07,344,39,14,22,228,45*7F"));
    TEST_ASSERT_EQUAL(NmeaParser::Sentence::Gsv,
                      parser.parse("$GLGSV,3,1,09,65,55,120,40,66,44,060,38,67,10,010,,72,30,290,36*6E"));
    TEST_ASSERT_EQUAL_UINT8(0, parser.fix().satellitesInView); // not yet folded in
    parser.parse(rmcNoFix);
    TEST_ASSERT_EQUAL_UINT8(9, parser.fix().satellitesInView);
    TEST_ASSERT_FALSE(parser.fix().valid);
    // Next epoch with no GSV at all reports zero again.
    parser.parse(rmcValid);
    TEST_ASSERT_EQUAL_UINT8(0, parser.fix().satellitesInView);
}

// --- assembler ----------------------------------------------------------

void assembler_delivers_sentence_without_line_ending()
{
    NmeaLineAssembler<96> assembler;
    size_t completed = 0;
    feed(assembler, "$GNGLL,1,2*00\r\n", completed);
    TEST_ASSERT_EQUAL_size_t(1, completed);
    TEST_ASSERT_EQUAL_STRING("$GNGLL,1,2*00", assembler.line());
}

void assembler_ignores_bytes_before_dollar_and_resyncs_mid_sentence()
{
    NmeaLineAssembler<96> assembler;
    size_t completed = 0;
    feed(assembler, "garbage\r\n$GNGLL,1,2*00\r\n$GNGLL,bro$GNGLL,3*11\n", completed);
    TEST_ASSERT_EQUAL_size_t(2, completed);
    TEST_ASSERT_EQUAL_STRING("$GNGLL,3*11", assembler.line());
}

void assembler_drops_and_counts_oversized_sentence()
{
    NmeaLineAssembler<16> assembler;
    size_t completed = 0;
    feed(assembler, "$GNGLL,0123456789012345*00\r\n$GNGLL,1*00\r\n", completed);
    TEST_ASSERT_EQUAL_size_t(1, completed);
    TEST_ASSERT_EQUAL_UINT32(1, assembler.overflows());
    TEST_ASSERT_EQUAL_STRING("$GNGLL,1*00", assembler.line());
}
