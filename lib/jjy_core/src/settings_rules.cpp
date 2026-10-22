#include "settings_rules.h"

#include <ctype.h>
#include <stdio.h>

#include "timezones.h"

DeviceSettings settingsDefaults() {
  // 01:16, 02:00, 03:16, 04:00, 13:16, 15:16, 16:16
  static const uint16_t kDefaultSlots[] = {76, 120, 196, 240, 796, 916, 976};
  static const uint8_t kDefaultCount = sizeof(kDefaultSlots) / sizeof(kDefaultSlots[0]);
  DeviceSettings s;
  s.tzId = "warsaw";
  s.tzRule = tzFindById("warsaw")->posix;
  for (uint8_t i = 0; i < kDefaultCount; ++i) s.schedule.slotMinutes[i] = kDefaultSlots[i];
  s.schedule.count = kDefaultCount;
  s.schedule.windowMinutes = 5;
  return s;
}

bool settingsValidWifi(const std::string &ssid, const std::string &pass, std::string &error) {
  if (ssid.empty()) {
    error = "Please choose a WiFi network.";
    return false;
  }
  if (ssid.size() > 32) {
    error = "WiFi network names are at most 32 characters.";
    return false;
  }
  if (!pass.empty() && (pass.size() < 8 || pass.size() > 63)) {
    error = "WiFi passwords are 8 to 63 characters long.";
    return false;
  }
  return true;
}

bool settingsValidTzRule(const std::string &rule) {
  if (rule.size() < 3 || rule.size() > 64) return false;
  if (!isalpha(static_cast<unsigned char>(rule[0])) && rule[0] != '<') return false;
  for (char c : rule) {
    if (c < 0x21 || c > 0x7e) return false;
  }
  return true;
}

bool settingsResolveTz(const std::string &id, const std::string &customRule, std::string &ruleOut) {
  if (id == "custom") {
    if (!settingsValidTzRule(customRule)) return false;
    ruleOut = customRule;
    return true;
  }
  const TzEntry *entry = tzFindById(id.c_str());
  if (entry == nullptr) return false;
  ruleOut = entry->posix;
  return true;
}

bool parseHHMM(const std::string &text, uint16_t &minutesOut) {
  size_t i = 0;
  int hour = 0;
  int digits = 0;
  while (i < text.size() && isdigit(static_cast<unsigned char>(text[i])) && digits < 2) {
    hour = hour * 10 + (text[i] - '0');
    ++i;
    ++digits;
  }
  if (digits == 0 || i >= text.size() || text[i] != ':') return false;
  ++i;
  int minute = 0;
  digits = 0;
  while (i < text.size() && isdigit(static_cast<unsigned char>(text[i])) && digits < 2) {
    minute = minute * 10 + (text[i] - '0');
    ++i;
    ++digits;
  }
  if (digits != 2) return false;
  if (i < text.size() && text[i] != ':') return false;
  if (hour > 23 || minute > 59) return false;
  minutesOut = static_cast<uint16_t>(hour * 60 + minute);
  return true;
}

std::string formatHHMM(uint16_t minutes) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%02u:%02u", static_cast<unsigned>(minutes / 60), static_cast<unsigned>(minutes % 60));
  return buf;
}
