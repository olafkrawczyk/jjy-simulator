#pragma once

#include <stdint.h>

#include <string>

#include "settings_rules.h"

enum class OtaState : uint8_t { IDLE, INSTALL_REQUESTED, INSTALLING, FAILED };

// Touched only from the Arduino loop task (setup, loop and web handlers).
struct AppState {
  DeviceSettings settings;
  bool unscheduledBroadcast = false;  // start-up or "Broadcast now" window; schedule edits don't stop it
  bool uiHold = false;
  uint32_t uiHoldLastMs = 0;  // uiHold expires 15 minutes after the last page request
  uint32_t sleepNotBeforeMs = 0;  // 0 = unset; keeps the device awake briefly after Save so the reply is delivered
  uint32_t restartAtMs = 0;  // 0 means no restart pending
  OtaState otaState = OtaState::IDLE;
  std::string otaError;
  std::string latestVersion;  // newer release found by the last check
  std::string latestUrl;
};

extern AppState app;
