#include <unity.h>

#include <string>

#include "settings_rules.h"
#include "version_compare.h"

void setUp() {}
void tearDown() {}

void test_defaults() {
  const DeviceSettings s = settingsDefaults();
  TEST_ASSERT_EQUAL_STRING("warsaw", s.tzId.c_str());
  TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0/2,M10.5.0/3", s.tzRule.c_str());
  TEST_ASSERT_EQUAL_UINT8(4, s.schedule.count);
  TEST_ASSERT_EQUAL_UINT16(76, s.schedule.slotMinutes[0]);
  TEST_ASSERT_EQUAL_UINT16(976, s.schedule.slotMinutes[3]);
  TEST_ASSERT_EQUAL_UINT8(5, s.schedule.windowMinutes);
  TEST_ASSERT_TRUE(s.wifiSsid.empty());
  TEST_ASSERT_EQUAL(ScheduleError::OK, scheduleValidate(s.schedule));
}

void test_wifi_rules() {
  std::string error;
  TEST_ASSERT_FALSE(settingsValidWifi("", "", error));
  TEST_ASSERT_FALSE(error.empty());
  TEST_ASSERT_FALSE(settingsValidWifi(std::string(33, 'a'), "", error));
  TEST_ASSERT_TRUE(settingsValidWifi(std::string(32, 'a'), "", error));
  TEST_ASSERT_FALSE(settingsValidWifi("Home", "1234567", error));
  TEST_ASSERT_TRUE(settingsValidWifi("Home", "12345678", error));
  TEST_ASSERT_TRUE(settingsValidWifi("Home", std::string(63, 'p'), error));
  TEST_ASSERT_FALSE(settingsValidWifi("Home", std::string(64, 'p'), error));
}

void test_tz_rule_shape() {
  TEST_ASSERT_TRUE(settingsValidTzRule("JST-9"));
  TEST_ASSERT_TRUE(settingsValidTzRule("<+03>-3"));
  TEST_ASSERT_FALSE(settingsValidTzRule(""));
  TEST_ASSERT_FALSE(settingsValidTzRule("has space"));
  TEST_ASSERT_FALSE(settingsValidTzRule("9abc"));
  TEST_ASSERT_FALSE(settingsValidTzRule(std::string(65, 'A')));
}

void test_resolve_tz() {
  std::string rule;
  TEST_ASSERT_TRUE(settingsResolveTz("tokyo", "", rule));
  TEST_ASSERT_EQUAL_STRING("JST-9", rule.c_str());
  TEST_ASSERT_TRUE(settingsResolveTz("custom", "UTC0", rule));
  TEST_ASSERT_EQUAL_STRING("UTC0", rule.c_str());
  TEST_ASSERT_FALSE(settingsResolveTz("custom", "bad rule", rule));
  TEST_ASSERT_FALSE(settingsResolveTz("atlantis", "", rule));
}

void test_parse_and_format_hhmm() {
  uint16_t m = 0;
  TEST_ASSERT_TRUE(parseHHMM("01:16", m));
  TEST_ASSERT_EQUAL_UINT16(76, m);
  TEST_ASSERT_TRUE(parseHHMM("1:16", m));
  TEST_ASSERT_EQUAL_UINT16(76, m);
  TEST_ASSERT_TRUE(parseHHMM("23:59", m));
  TEST_ASSERT_EQUAL_UINT16(1439, m);
  TEST_ASSERT_TRUE(parseHHMM("00:00", m));
  TEST_ASSERT_EQUAL_UINT16(0, m);
  TEST_ASSERT_TRUE(parseHHMM("01:16:30", m));
  TEST_ASSERT_EQUAL_UINT16(76, m);
  TEST_ASSERT_FALSE(parseHHMM("24:00", m));
  TEST_ASSERT_FALSE(parseHHMM("01:60", m));
  TEST_ASSERT_FALSE(parseHHMM("01:6", m));
  TEST_ASSERT_FALSE(parseHHMM("abc", m));
  TEST_ASSERT_FALSE(parseHHMM("", m));
  TEST_ASSERT_EQUAL_STRING("01:16", formatHHMM(76).c_str());
  TEST_ASSERT_EQUAL_STRING("00:00", formatHHMM(0).c_str());
  TEST_ASSERT_EQUAL_STRING("23:59", formatHHMM(1439).c_str());
}

void test_version_compare() {
  TEST_ASSERT_EQUAL_INT(1, versionCompare("1.2.0", "1.1.9"));
  TEST_ASSERT_EQUAL_INT(0, versionCompare("v1.2.0", "1.2.0"));
  TEST_ASSERT_EQUAL_INT(1, versionCompare("1.10.0", "1.9.0"));
  TEST_ASSERT_EQUAL_INT(1, versionCompare("2", "1.9.9"));
  TEST_ASSERT_EQUAL_INT(1, versionCompare("1.0.0", "dev"));
  TEST_ASSERT_EQUAL_INT(-1, versionCompare("dev", "1.0.0"));
  TEST_ASSERT_EQUAL_INT(0, versionCompare("dev", "dev"));
  TEST_ASSERT_EQUAL_INT(0, versionCompare("1.0.0-rc1", "1.0.0"));
  TEST_ASSERT_EQUAL_INT(1, versionCompare("99999999999.0.0", "1.0.0"));
  TEST_ASSERT_EQUAL_INT(-1, versionCompare(nullptr, "1.0.0"));
  TEST_ASSERT_EQUAL_INT(-1, versionCompare("", "1.0.0"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults);
  RUN_TEST(test_wifi_rules);
  RUN_TEST(test_tz_rule_shape);
  RUN_TEST(test_resolve_tz);
  RUN_TEST(test_parse_and_format_hhmm);
  RUN_TEST(test_version_compare);
  return UNITY_END();
}
