#pragma once

#include <stdint.h>

void timeSyncSetTz(const char *tzRule);
void timeSyncBegin(const char *tzRule);
bool timeSyncWaitForNtp(uint32_t timeoutMs);
bool timeSyncClockValid();
