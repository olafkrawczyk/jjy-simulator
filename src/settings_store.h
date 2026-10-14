#pragma once

#include <stdint.h>

#include "settings_rules.h"

void settingsLoad(DeviceSettings &settings);
void settingsSave(const DeviceSettings &settings);
void settingsForgetWifi();
uint8_t settingsIncrementBootCount();
void settingsClearBootCount();
