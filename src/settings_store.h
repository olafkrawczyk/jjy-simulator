#pragma once

#include <stdint.h>

#include "settings_rules.h"

void settingsLoad(DeviceSettings &settings);
bool settingsSave(const DeviceSettings &settings);
bool settingsForgetWifi();
uint8_t settingsIncrementBootCount();
void settingsClearBootCount();
