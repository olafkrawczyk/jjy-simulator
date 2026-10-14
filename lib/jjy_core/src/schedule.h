#pragma once

#include <stdint.h>
#include <time.h>

static const uint8_t kMaxSlots = 8;
static const uint16_t kMinutesPerDay = 1440;
static const uint8_t kWindowMinMinutes = 1;
static const uint8_t kWindowMaxMinutes = 30;
static const uint32_t kWakeLeadSec = 120;
static const uint32_t kMinSleepSec = 30;

struct Schedule {
  uint16_t slotMinutes[kMaxSlots];
  uint8_t count;
  uint8_t windowMinutes;
};

enum class ScheduleError : uint8_t { OK, EMPTY, TOO_MANY, OUT_OF_RANGE, DUPLICATE, WINDOW_LENGTH, OVERLAP };

void scheduleSort(Schedule &s);
ScheduleError scheduleValidate(const Schedule &s);
const char *scheduleErrorText(ScheduleError e);
bool scheduleEquals(const Schedule &a, const Schedule &b);

// The functions below interpret slot times in the current TZ.
bool scheduleCurrentWindow(const Schedule &s, time_t now, time_t &startOut, time_t &endOut);
time_t scheduleNextStart(const Schedule &s, time_t now);
bool scheduleSleepUntil(const Schedule &s, time_t now, time_t &wakeOut);
