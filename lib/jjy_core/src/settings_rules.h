#pragma once

#include <stdint.h>

#include <string>

#include "schedule.h"

struct DeviceSettings {
  std::string wifiSsid;
  std::string wifiPass;
  std::string tzId;
  std::string tzRule;
  Schedule schedule = Schedule();
};

DeviceSettings settingsDefaults();
bool settingsValidWifi(const std::string &ssid, const std::string &pass, std::string &error);
bool settingsValidTzRule(const std::string &rule);
bool settingsResolveTz(const std::string &id, const std::string &customRule, std::string &ruleOut);
bool parseHHMM(const std::string &text, uint16_t &minutesOut);
std::string formatHHMM(uint16_t minutes);
