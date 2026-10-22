#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include "schedule.h"

struct SetupPageView {
  std::vector<std::string> networks;
  std::string selectedSsid;
  std::string tzId;
  std::string customTzRule;
  Schedule schedule = Schedule();
  bool autoDetectTz = false;
  std::string error;
};

struct SettingsPageView {
  std::string localTime;      // empty while the clock is not valid
  std::string nextBroadcast;  // "HH:MM", empty if unknown
  bool broadcasting = false;
  std::string broadcastUntil;  // "HH:MM" end of the current broadcast, empty if unknown
  uint32_t windowElapsedSec = 0;  // progress of the current broadcast; total 0 hides the bar
  uint32_t windowTotalSec = 0;
  std::string tzId;
  std::string customTzRule;
  Schedule schedule = Schedule();
  std::string hostname;
  std::string version;
  std::string updateVersion;  // non-empty shows the Install button
  std::string notice;
  std::string error;
};

std::string htmlEscape(const std::string &text);
std::string jsonEscape(const std::string &text);
std::string renderSetupPage(const SetupPageView &view);
std::string renderConnectingPage(const std::string &hostname);
std::string renderSettingsPage(const SettingsPageView &view);
// Live part of the settings page: time, broadcast progress and next broadcast.
std::string renderWindowJson(const SettingsPageView &view);
std::string renderUpdatingPage(const std::string &currentVersion);
std::string renderMessagePage(const std::string &title, const std::string &text);
