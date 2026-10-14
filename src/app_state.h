#pragma once

#include <stdint.h>

#include <string>

#include "settings_rules.h"

enum class OtaState : uint8_t { IDLE, INSTALL_REQUESTED, INSTALLING, FAILED };

// Touched only from the Arduino loop task (setup, loop and web handlers).
struct AppState {
  DeviceSettings settings;
  bool coldBootBroadcast = false;
  bool uiHold = false;
  uint32_t restartAtMs = 0;  // 0 means no restart pending
  OtaState otaState = OtaState::IDLE;
  std::string otaError;
  std::string latestVersion;  // newer release found by the last check
  std::string latestUrl;
};

extern AppState app;
