#include "time_sync.h"

#include <Arduino.h>
#include <esp_sntp.h>
#include <time.h>

namespace {
const char *kNtp1 = "pool.ntp.org";
const char *kNtp2 = "time.google.com";
const time_t kMinValidEpoch = 1700000000;  // 2023-11-14
}  // namespace

void timeSyncSetTz(const char *tzRule) {
  setenv("TZ", tzRule, 1);
  tzset();
}

void timeSyncBegin(const char *tzRule) {
  sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
  configTzTime(tzRule, kNtp1, kNtp2);
}

bool timeSyncWaitForNtp(uint32_t timeoutMs) {
  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) return true;
    delay(100);
  }
  return false;
}

bool timeSyncClockValid() { return time(nullptr) >= kMinValidEpoch; }
