#pragma once

#include <stddef.h>

#include <string>
struct TzEntry {
  const char *id;
  const char *label;
  const char *iana;  // space-separated IANA names the browser may report
  const char *posix;
};

extern const TzEntry kTimezones[];
extern const size_t kTimezoneCount;

const TzEntry *tzFindById(const char *id);

// Standard (non-DST) offset of a POSIX rule as "GMT+1", "GMT-3", "GMT+5:30"; empty if unparsable.
std::string tzStandardOffsetLabel(const char *posix);
