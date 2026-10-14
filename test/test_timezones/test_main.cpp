#include <unity.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "timezones.h"

void setUp() {}
void tearDown() {}

struct Expected {
  const char *id;
  long january;
  long july;
};

// UTC offsets in seconds at 2026-01-15 12:00 UTC and 2026-07-15 12:00 UTC.
static const Expected kExpected[] = {
    {"honolulu", -36000, -36000},   {"anchorage", -32400, -28800},  {"los_angeles", -28800, -25200},
    {"denver", -25200, -21600},     {"phoenix", -25200, -25200},    {"chicago", -21600, -18000},
    {"mexico_city", -21600, -21600}, {"new_york", -18000, -14400},  {"halifax", -14400, -10800},
    {"sao_paulo", -10800, -10800},  {"buenos_aires", -10800, -10800}, {"reykjavik", 0, 0},
    {"london", 0, 3600},            {"dublin", 0, 3600},            {"lisbon", 0, 3600},
    {"madrid", 3600, 7200},         {"paris", 3600, 7200},          {"amsterdam", 3600, 7200},
    {"berlin", 3600, 7200},         {"rome", 3600, 7200},           {"stockholm", 3600, 7200},
    {"prague", 3600, 7200},         {"warsaw", 3600, 7200},         {"lagos", 3600, 3600},
    {"athens", 7200, 10800},        {"bucharest", 7200, 10800},     {"helsinki", 7200, 10800},
    {"kyiv", 7200, 10800},          {"cairo", 7200, 10800},         {"johannesburg", 7200, 7200},
    {"istanbul", 10800, 10800},     {"moscow", 10800, 10800},       {"nairobi", 10800, 10800},
    {"dubai", 14400, 14400},        {"karachi", 18000, 18000},      {"delhi", 19800, 19800},
    {"dhaka", 21600, 21600},        {"bangkok", 25200, 25200},      {"jakarta", 25200, 25200},
    {"singapore", 28800, 28800},    {"hong_kong", 28800, 28800},    {"shanghai", 28800, 28800},
    {"taipei", 28800, 28800},       {"manila", 28800, 28800},       {"perth", 28800, 28800},
    {"seoul", 32400, 32400},        {"tokyo", 32400, 32400},        {"adelaide", 37800, 34200},
    {"brisbane", 36000, 36000},     {"sydney", 39600, 36000},       {"auckland", 46800, 43200},
    {"utc", 0, 0},
};

static long offsetAt(const char *posix, time_t epoch) {
  setenv("TZ", posix, 1);
  tzset();
  struct tm t;
  localtime_r(&epoch, &t);
  return t.tm_gmtoff;
}

void test_every_entry_has_expected_offsets() {
  const time_t january = 1768478400;  // 2026-01-15 12:00:00 UTC
  const time_t july = 1784116800;     // 2026-07-15 12:00:00 UTC
  const size_t expectedCount = sizeof(kExpected) / sizeof(kExpected[0]);
  TEST_ASSERT_EQUAL_UINT32(expectedCount, kTimezoneCount);
  for (size_t i = 0; i < kTimezoneCount; ++i) {
    const TzEntry &e = kTimezones[i];
    const Expected *found = nullptr;
    for (size_t j = 0; j < expectedCount; ++j) {
      if (strcmp(kExpected[j].id, e.id) == 0) found = &kExpected[j];
    }
    TEST_ASSERT_NOT_NULL_MESSAGE(found, e.id);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(found->january, offsetAt(e.posix, january), e.id);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(found->july, offsetAt(e.posix, july), e.id);
  }
}

void test_ids_unique_and_labels_present() {
  for (size_t i = 0; i < kTimezoneCount; ++i) {
    TEST_ASSERT_TRUE(strlen(kTimezones[i].label) > 0);
    TEST_ASSERT_TRUE(strlen(kTimezones[i].iana) > 0);
    for (size_t j = i + 1; j < kTimezoneCount; ++j) {
      TEST_ASSERT_TRUE_MESSAGE(strcmp(kTimezones[i].id, kTimezones[j].id) != 0, kTimezones[i].id);
    }
  }
}

void test_find_by_id() {
  TEST_ASSERT_EQUAL_STRING("JST-9", tzFindById("tokyo")->posix);
  TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0/2,M10.5.0/3", tzFindById("warsaw")->posix);
  TEST_ASSERT_NULL(tzFindById("atlantis"));
  TEST_ASSERT_NULL(tzFindById("custom"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_every_entry_has_expected_offsets);
  RUN_TEST(test_ids_unique_and_labels_present);
  RUN_TEST(test_find_by_id);
  return UNITY_END();
}
