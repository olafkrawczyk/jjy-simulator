#include <unity.h>

#include <string>

#include "jjy_frame.h"

void setUp() {}
void tearDown() {}

static struct tm makeTm(int year, int yday0, int wday, int hour, int minute) {
  struct tm t = {};
  t.tm_year = year - 1900;
  t.tm_yday = yday0;
  t.tm_wday = wday;
  t.tm_hour = hour;
  t.tm_min = minute;
  return t;
}

static std::string frameString(const struct tm &t) {
  JjySymbol frame[kJjyFrameLength];
  jjyBuildFrame(t, frame);
  std::string s;
  for (int i = 0; i < kJjyFrameLength; ++i) s += jjySymbolChar(frame[i]);
  return s;
}

// 2026-10-14 13:16, day 287 (tm_yday 286), Wednesday.
// min 16 -> 001 0110, hour 13 -> 01 0011, day 287 -> 10 1000 0111,
// PA1 = parity(hour bits) = 1, PA2 = parity(minute bits) = 1, year 26 -> 0010 0110, weekday 3 -> 011.
void test_reference_frame() {
  const char *expected =
      "P00100110P"
      "000100011P"
      "001001000P"
      "011100110P"
      "000100110P"
      "011000000P";
  TEST_ASSERT_EQUAL_STRING(expected, frameString(makeTm(2026, 286, 3, 13, 16)).c_str());
}

void test_markers_at_fixed_positions() {
  const std::string s = frameString(makeTm(2025, 0, 3, 0, 0));
  const int markers[] = {0, 9, 19, 29, 39, 49, 59};
  for (int m : markers) TEST_ASSERT_EQUAL_CHAR('P', s[m]);
}

// 2024-12-31 23:59 is day 366: hundreds 3 -> bits 22,23 = 1,1; tens 6 -> 25..28 = 0110; ones 6 -> 30..33 = 0110.
void test_day_366() {
  const std::string s = frameString(makeTm(2024, 365, 2, 23, 59));
  TEST_ASSERT_EQUAL_STRING("11", s.substr(22, 2).c_str());
  TEST_ASSERT_EQUAL_STRING("0110", s.substr(25, 4).c_str());
  TEST_ASSERT_EQUAL_STRING("0110", s.substr(30, 4).c_str());
}

void test_on_durations() {
  TEST_ASSERT_EQUAL_UINT16(200, jjyOnDurationMs(JjySymbol::MARKER));
  TEST_ASSERT_EQUAL_UINT16(500, jjyOnDurationMs(JjySymbol::ONE));
  TEST_ASSERT_EQUAL_UINT16(800, jjyOnDurationMs(JjySymbol::ZERO));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_reference_frame);
  RUN_TEST(test_markers_at_fixed_positions);
  RUN_TEST(test_day_366);
  RUN_TEST(test_on_durations);
  return UNITY_END();
}
