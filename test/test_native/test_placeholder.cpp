// Host-side unit tests for the pure-logic pieces (no ESP-IDF required):
//   pio test -e native
// The NMEA parser, filters and geodesy helpers get their tests here once
// they exist; this placeholder keeps the native environment wired up.
#include <unity.h>

void setUp() {}
void tearDown() {}

static void native_test_environment_runs()
{
    TEST_ASSERT_TRUE(true);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(native_test_environment_runs);
    return UNITY_END();
}
