#pragma once

#include <stddef.h>

struct TzEntry {
  const char *id;
  const char *label;
  const char *iana;  // space-separated IANA names the browser may report
  const char *posix;
};

extern const TzEntry kTimezones[];
extern const size_t kTimezoneCount;

const TzEntry *tzFindById(const char *id);
