#include <unity.h>
#include <Arduino.h>
#include "config.h"
#include "nvs_storage.h"
#include "rtc_manager.h"
#include "relay_controller.h"
#include "schedule_manager.h"

void setUp(void) {
}

void tearDown(void) {
}

void test_mock_relay_hal(void) {
    RelayController test_rc(true /* is_mock */);
    test_rc.initPins();
    TEST_ASSERT_TRUE(test_rc.isMockMode());
#ifdef ENABLE_FAULT_INJECTION_TEST
    TEST_ASSERT_TRUE(test_rc.testFaultInjectionEmergency(0));
#endif
}

void test_mock_nvs_and_rtc(void) {
    NvsStorage test_nvs(true /* is_mock */);
    TEST_ASSERT_TRUE(test_nvs.begin());
    TEST_ASSERT_TRUE(test_nvs.isMockMode());

    RelayProfile profiles[TOTAL_RELAYS];
    TEST_ASSERT_TRUE(test_nvs.loadAllProfiles(profiles));
    TEST_ASSERT_EQUAL_UINT32(DEFAULT_SPRAY_DAY_S, profiles[0].spray_day_s);

    RtcManager test_rtc(true /* is_mock */);
    TEST_ASSERT_TRUE(test_rtc.begin());
    TEST_ASSERT_TRUE(test_rtc.isMockMode());

    SystemTime t = test_rtc.getTime();
    TEST_ASSERT_TRUE(t.is_valid);
    TEST_ASSERT_FALSE(test_rtc.isNightMode());
}

void test_mock_schedule_override(void) {
#ifdef ENABLE_FAULT_INJECTION_TEST
    ScheduleManager test_sm;
    TEST_ASSERT_TRUE(test_sm.testOverridePauseResume());
#endif
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_mock_relay_hal);
    RUN_TEST(test_mock_nvs_and_rtc);
    RUN_TEST(test_mock_schedule_override);
    UNITY_END();
}

void loop() {
}
