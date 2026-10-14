#include <unity.h>

#include <stdlib.h>
#include <time.h>

#include <initializer_list>

#include "schedule.h"

void setUp() {
  setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
  tzset();
}
void tearDown() {}

static time_t local(int y, int mo, int d, int h, int mi, int s = 0) {
  struct tm t = {};
  t.tm_year = y - 1900;
  t.tm_mon = mo - 1;
  t.tm_mday = d;
  t.tm_hour = h;
  t.tm_min = mi;
  t.tm_sec = s;
  t.tm_isdst = -1;
  return mktime(&t);
}

static struct tm parts(time_t epoch) {
  struct tm t;
  localtime_r(&epoch, &t);
  return t;
}

static Schedule make(std::initializer_list<uint16_t> slots, uint8_t windowMinutes) {
  Schedule s = Schedule();
  for (uint16_t m : slots) s.slotMinutes[s.count++] = m;
  s.windowMinutes = windowMinutes;
  return s;
}

static Schedule defaults() { return make({76, 256, 796, 976}, 5); }

void test_validate_defaults_ok() { TEST_ASSERT_EQUAL(ScheduleError::OK, scheduleValidate(defaults())); }

void test_validate_errors() {
  TEST_ASSERT_EQUAL(ScheduleError::EMPTY, scheduleValidate(make({}, 5)));
  Schedule tooMany = defaults();
  tooMany.count = 9;
  TEST_ASSERT_EQUAL(ScheduleError::TOO_MANY, scheduleValidate(tooMany));
  TEST_ASSERT_EQUAL(ScheduleError::OUT_OF_RANGE, scheduleValidate(make({1440}, 5)));
  TEST_ASSERT_EQUAL(ScheduleError::DUPLICATE, scheduleValidate(make({600, 600}, 5)));
  TEST_ASSERT_EQUAL(ScheduleError::WINDOW_LENGTH, scheduleValidate(make({600}, 0)));
  TEST_ASSERT_EQUAL(ScheduleError::WINDOW_LENGTH, scheduleValidate(make({600}, 31)));
  TEST_ASSERT_EQUAL(ScheduleError::OVERLAP, scheduleValidate(make({600, 603}, 5)));
  TEST_ASSERT_EQUAL(ScheduleError::OVERLAP, scheduleValidate(make({1438, 1}, 5)));
}

void test_validate_accepts_unsorted_and_touching() {
  TEST_ASSERT_EQUAL(ScheduleError::OK, scheduleValidate(make({600, 60}, 5)));
  TEST_ASSERT_EQUAL(ScheduleError::OK, scheduleValidate(make({600, 605}, 5)));
}

void test_sort_and_equals() {
  Schedule s = make({796, 76}, 5);
  scheduleSort(s);
  TEST_ASSERT_EQUAL_UINT16(76, s.slotMinutes[0]);
  TEST_ASSERT_TRUE(scheduleEquals(s, make({76, 796}, 5)));
  TEST_ASSERT_FALSE(scheduleEquals(s, make({76, 796}, 6)));
  TEST_ASSERT_FALSE(scheduleEquals(s, make({76}, 5)));
}

void test_current_window_inside_and_edges() {
  time_t start, end;
  TEST_ASSERT_TRUE(scheduleCurrentWindow(defaults(), local(2026, 10, 14, 13, 18), start, end));
  TEST_ASSERT_EQUAL_INT64(local(2026, 10, 14, 13, 16), start);
  TEST_ASSERT_EQUAL_INT64(local(2026, 10, 14, 13, 21), end);
  TEST_ASSERT_TRUE(scheduleCurrentWindow(defaults(), local(2026, 10, 14, 13, 16), start, end));
  TEST_ASSERT_FALSE(scheduleCurrentWindow(defaults(), local(2026, 10, 14, 13, 21), start, end));
  TEST_ASSERT_FALSE(scheduleCurrentWindow(defaults(), local(2026, 10, 14, 12, 0), start, end));
}

void test_current_window_across_midnight() {
  time_t start, end;
  TEST_ASSERT_TRUE(scheduleCurrentWindow(make({1438}, 5), local(2026, 10, 15, 0, 1), start, end));
  TEST_ASSERT_EQUAL_INT64(local(2026, 10, 14, 23, 58), start);
  TEST_ASSERT_EQUAL_INT64(local(2026, 10, 15, 0, 3), end);
}

void test_next_start() {
  TEST_ASSERT_EQUAL_INT64(local(2026, 10, 14, 16, 16), scheduleNextStart(defaults(), local(2026, 10, 14, 13, 16)));
  TEST_ASSERT_EQUAL_INT64(local(2026, 10, 15, 1, 16), scheduleNextStart(defaults(), local(2026, 10, 14, 17, 0)));
}

void test_dst_spring_forward_missing_time() {
  // 2026-03-29 02:00 CET jumps to 03:00 CEST; 02:30 does not exist and becomes 03:30.
  const time_t next = scheduleNextStart(make({150}, 5), local(2026, 3, 29, 0, 0));
  const struct tm t = parts(next);
  TEST_ASSERT_EQUAL_INT(29, t.tm_mday);
  TEST_ASSERT_EQUAL_INT(3, t.tm_hour);
  TEST_ASSERT_EQUAL_INT(30, t.tm_min);
}

void test_dst_fall_back_runs_once() {
  // 2026-10-25 03:00 CEST falls back to 02:00 CET; 02:30 happens twice but must broadcast once.
  const Schedule s = make({150}, 5);
  const time_t first = scheduleNextStart(s, local(2026, 10, 25, 0, 0));
  TEST_ASSERT_EQUAL_INT(25, parts(first).tm_mday);
  const time_t second = scheduleNextStart(s, first);
  TEST_ASSERT_EQUAL_INT(26, parts(second).tm_mday);
}

void test_dst_fall_back_second_occurrence_not_a_window() {
  // One hour after the first 02:30 the wall clock reads 02:30 again (CET); that must not start a new window.
  const Schedule s = make({150}, 5);
  const time_t first = scheduleNextStart(s, local(2026, 10, 25, 0, 0));
  time_t start, end;
  TEST_ASSERT_TRUE(scheduleCurrentWindow(s, first + 60, start, end));
  TEST_ASSERT_EQUAL_INT64(first, start);
  TEST_ASSERT_FALSE(scheduleCurrentWindow(s, first + 3600 + 60, start, end));
}

void test_window_across_spring_forward_jump() {
  time_t start, end;
  const Schedule s = make({118}, 5);
  TEST_ASSERT_TRUE(scheduleCurrentWindow(s, local(2026, 3, 29, 1, 59), start, end));
  TEST_ASSERT_EQUAL_INT64(300, end - start);
  TEST_ASSERT_TRUE(scheduleCurrentWindow(s, start + 240, start, end));
}

void test_zone_without_dst() {
  setenv("TZ", "JST-9", 1);
  tzset();
  const time_t next = scheduleNextStart(make({76}, 5), local(2026, 3, 29, 0, 0));
  const struct tm t = parts(next);
  TEST_ASSERT_EQUAL_INT(29, t.tm_mday);
  TEST_ASSERT_EQUAL_INT(1, t.tm_hour);
  TEST_ASSERT_EQUAL_INT(16, t.tm_min);
}

void test_sleep_until() {
  time_t wake = 0;
  TEST_ASSERT_TRUE(scheduleSleepUntil(defaults(), local(2026, 10, 14, 12, 0), wake));
  TEST_ASSERT_EQUAL_INT64(local(2026, 10, 14, 13, 14), wake);
  TEST_ASSERT_FALSE(scheduleSleepUntil(defaults(), local(2026, 10, 14, 13, 15), wake));
  TEST_ASSERT_TRUE(scheduleSleepUntil(defaults(), local(2026, 10, 14, 13, 13, 30), wake));
  TEST_ASSERT_EQUAL_INT64(local(2026, 10, 14, 13, 14), wake);
  TEST_ASSERT_FALSE(scheduleSleepUntil(defaults(), local(2026, 10, 14, 13, 13, 40), wake));
}

void test_close_windows_stay_awake() {
  // Window 10:00-10:01 ends; the next starts at 10:02 so the wake time (10:00) has passed.
  time_t wake = 0;
  TEST_ASSERT_FALSE(scheduleSleepUntil(make({600, 602}, 1), local(2026, 10, 14, 10, 1), wake));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_validate_defaults_ok);
  RUN_TEST(test_validate_errors);
  RUN_TEST(test_validate_accepts_unsorted_and_touching);
  RUN_TEST(test_sort_and_equals);
  RUN_TEST(test_current_window_inside_and_edges);
  RUN_TEST(test_current_window_across_midnight);
  RUN_TEST(test_next_start);
  RUN_TEST(test_dst_spring_forward_missing_time);
  RUN_TEST(test_dst_fall_back_runs_once);
  RUN_TEST(test_dst_fall_back_second_occurrence_not_a_window);
  RUN_TEST(test_window_across_spring_forward_jump);
  RUN_TEST(test_zone_without_dst);
  RUN_TEST(test_sleep_until);
  RUN_TEST(test_close_windows_stay_awake);
  return UNITY_END();
}
