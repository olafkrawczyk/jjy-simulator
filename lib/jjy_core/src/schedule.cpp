#include "schedule.h"

#include <algorithm>

namespace {

// Days since 1970-01-01 for a proleptic Gregorian date; month 1..12, day may overflow the month.
int64_t daysFromCivil(int64_t year, int month, int64_t day) {
  year -= month <= 2;
  const int64_t era = (year >= 0 ? year : year - 399) / 400;
  const int64_t yearOfEra = year - era * 400;
  const int64_t dayOfYear = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5;
  const int64_t dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return era * 146097 + dayOfEra - 719468 + (day - 1);
}

int64_t wallSeconds(const struct tm &t) {
  return daysFromCivil(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday) * 86400 + t.tm_hour * 3600 + t.tm_min * 60 +
         t.tm_sec;
}

int64_t utcOffsetAt(int64_t instant) {
  const time_t t = static_cast<time_t>(instant);
  struct tm local;
  localtime_r(&t, &local);
  return wallSeconds(local) - instant;
}

// Resolves a wall-clock slot to an instant without mktime, so glibc and newlib agree.
// A time that occurs twice (DST fall-back) resolves to its first occurrence; a time that
// does not exist (spring-forward gap) moves forward by the gap, e.g. 02:30 -> 03:30.
time_t slotStart(const struct tm &base, uint16_t minutes, int dayOffset) {
  const int64_t wall =
      daysFromCivil(base.tm_year + 1900, base.tm_mon + 1, base.tm_mday + dayOffset) * 86400 + minutes * 60;
  const int64_t offsetBefore = utcOffsetAt(wall - 86400);
  const int64_t offsetAfter = utcOffsetAt(wall + 86400);
  const int64_t offsets[] = {offsetBefore, offsetAfter};
  bool found = false;
  int64_t best = 0;
  for (int64_t offset : offsets) {
    const int64_t instant = wall - offset;
    if (utcOffsetAt(instant) == offset && (!found || instant < best)) {
      best = instant;
      found = true;
    }
  }
  return static_cast<time_t>(found ? best : wall - offsetBefore);
}

}  // namespace

void scheduleSort(Schedule &s) {
  if (s.count > kMaxSlots) return;
  std::sort(s.slotMinutes, s.slotMinutes + s.count);
}

ScheduleError scheduleValidate(const Schedule &input) {
  if (input.count == 0) return ScheduleError::EMPTY;
  if (input.count > kMaxSlots) return ScheduleError::TOO_MANY;
  if (input.windowMinutes < kWindowMinMinutes || input.windowMinutes > kWindowMaxMinutes) {
    return ScheduleError::WINDOW_LENGTH;
  }
  Schedule s = input;
  scheduleSort(s);
  for (uint8_t i = 0; i < s.count; ++i) {
    if (s.slotMinutes[i] >= kMinutesPerDay) return ScheduleError::OUT_OF_RANGE;
  }
  for (uint8_t i = 1; i < s.count; ++i) {
    if (s.slotMinutes[i] == s.slotMinutes[i - 1]) return ScheduleError::DUPLICATE;
    if (s.slotMinutes[i] - s.slotMinutes[i - 1] < s.windowMinutes) return ScheduleError::OVERLAP;
  }
  if (s.count > 1) {
    const int wrapGap = s.slotMinutes[0] + kMinutesPerDay - s.slotMinutes[s.count - 1];
    if (wrapGap < s.windowMinutes) return ScheduleError::OVERLAP;
  }
  return ScheduleError::OK;
}

const char *scheduleErrorText(ScheduleError e) {
  switch (e) {
    case ScheduleError::OK:
      return "";
    case ScheduleError::EMPTY:
      return "Add at least one start time.";
    case ScheduleError::TOO_MANY:
      return "You can add up to 8 start times.";
    case ScheduleError::OUT_OF_RANGE:
      return "Please enter times like 01:16.";
    case ScheduleError::DUPLICATE:
      return "Each start time can only appear once.";
    case ScheduleError::WINDOW_LENGTH:
      return "Broadcast length must be between 1 and 30 minutes.";
    case ScheduleError::OVERLAP:
      return "Broadcasts would overlap. Space the start times further apart or shorten the broadcast.";
  }
  return "";
}

bool scheduleEquals(const Schedule &a, const Schedule &b) {
  if (a.count != b.count || a.windowMinutes != b.windowMinutes) return false;
  for (uint8_t i = 0; i < a.count && i < kMaxSlots; ++i) {
    if (a.slotMinutes[i] != b.slotMinutes[i]) return false;
  }
  return true;
}

bool scheduleCurrentWindow(const Schedule &s, time_t now, time_t &startOut, time_t &endOut) {
  struct tm base;
  localtime_r(&now, &base);
  for (int day = -1; day <= 0; ++day) {
    for (uint8_t i = 0; i < s.count && i < kMaxSlots; ++i) {
      const time_t start = slotStart(base, s.slotMinutes[i], day);
      const time_t end = start + static_cast<time_t>(s.windowMinutes) * 60;
      if (now >= start && now < end) {
        startOut = start;
        endOut = end;
        return true;
      }
    }
  }
  return false;
}

time_t scheduleNextStart(const Schedule &s, time_t now) {
  struct tm base;
  localtime_r(&now, &base);
  time_t best = 0;
  for (int day = 0; day <= 1; ++day) {
    for (uint8_t i = 0; i < s.count && i < kMaxSlots; ++i) {
      const time_t start = slotStart(base, s.slotMinutes[i], day);
      if (start > now && (best == 0 || start < best)) best = start;
    }
  }
  return best;
}

bool scheduleSleepUntil(const Schedule &s, time_t now, time_t &wakeOut) {
  const time_t next = scheduleNextStart(s, now);
  if (next == 0) return false;
  const time_t wake = next - static_cast<time_t>(kWakeLeadSec);
  if (wake - now < static_cast<time_t>(kMinSleepSec)) return false;
  wakeOut = wake;
  return true;
}
