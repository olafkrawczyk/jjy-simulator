#include "schedule.h"

#include <algorithm>

namespace {

// Resolves a wall-clock slot to an instant. When the time occurs twice (DST fall-back),
// returns the earlier occurrence so the slot broadcasts once; when it does not exist
// (spring-forward gap), lets mktime move it forward.
time_t slotStart(const struct tm &base, uint16_t minutes, int dayOffset) {
  struct tm t = base;
  t.tm_mday += dayOffset;
  t.tm_hour = minutes / 60;
  t.tm_min = minutes % 60;
  t.tm_sec = 0;

  time_t best = 0;
  bool found = false;
  for (int isdst = 0; isdst <= 1; ++isdst) {
    struct tm candidate = t;
    candidate.tm_isdst = isdst;
    const time_t epoch = mktime(&candidate);
    const bool sameWallTime = candidate.tm_isdst == isdst && candidate.tm_hour == t.tm_hour && candidate.tm_min == t.tm_min;
    if (epoch != static_cast<time_t>(-1) && sameWallTime && (!found || epoch < best)) {
      best = epoch;
      found = true;
    }
  }
  if (found) return best;

  t.tm_isdst = -1;
  return mktime(&t);
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
