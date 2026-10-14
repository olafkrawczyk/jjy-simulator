#include "settings_store.h"

#include <Arduino.h>
#include <Preferences.h>

#include "timezones.h"

namespace {

Preferences prefs;
bool ready = false;

bool open() {
  if (!ready) ready = prefs.begin("jjy", false);
  return ready;
}

std::string readString(const char *key) {
  if (!prefs.isKey(key)) return std::string();
  return std::string(prefs.getString(key, "").c_str());
}

}  // namespace

void settingsLoad(DeviceSettings &s) {
  s = settingsDefaults();
  if (!open()) {
    Serial.println("[NVS] Could not open settings; using defaults.");
    return;
  }

  s.wifiSsid = readString("wifi_ssid");
  s.wifiPass = readString("wifi_pass");

  std::string tzId = readString("tz_id");
  std::string tzRule = readString("tz_rule");
  if (tzId.empty() && !tzRule.empty()) {
    tzId = tzRule == tzFindById("warsaw")->posix ? "warsaw" : "custom";
    Serial.printf("[NVS] Migrated time zone rule to id '%s'\n", tzId.c_str());
  }
  std::string resolved;
  if (!tzId.empty() && settingsResolveTz(tzId, tzRule, resolved)) {
    s.tzId = tzId;
    s.tzRule = resolved;
  }

  if (prefs.isKey("web_ovrd")) prefs.remove("web_ovrd");

  const size_t length = prefs.isKey("slots") ? prefs.getBytesLength("slots") : 0;
  if (length >= sizeof(uint16_t) && length <= sizeof(s.schedule.slotMinutes) && length % sizeof(uint16_t) == 0) {
    Schedule loaded = Schedule();
    prefs.getBytes("slots", loaded.slotMinutes, length);
    loaded.count = static_cast<uint8_t>(length / sizeof(uint16_t));
    loaded.windowMinutes = prefs.getUChar("win_min", 5);
    if (scheduleValidate(loaded) == ScheduleError::OK) {
      scheduleSort(loaded);
      s.schedule = loaded;
    }
  }

  const uint32_t hz = prefs.isKey("freq_hz") ? prefs.getUInt("freq_hz", kFreqDefaultHz) : kFreqDefaultHz;
  if (settingsValidFreq(hz)) s.freqHz = hz;

  Serial.printf("[NVS] WiFi '%s', time zone %s, %u start times, %lu Hz\n", s.wifiSsid.c_str(), s.tzId.c_str(),
                static_cast<unsigned>(s.schedule.count), static_cast<unsigned long>(s.freqHz));
}

void settingsSave(const DeviceSettings &s) {
  if (!open()) return;
  prefs.putString("wifi_ssid", s.wifiSsid.c_str());
  prefs.putString("wifi_pass", s.wifiPass.c_str());
  prefs.putString("tz_id", s.tzId.c_str());
  prefs.putString("tz_rule", s.tzRule.c_str());
  prefs.putBytes("slots", s.schedule.slotMinutes, s.schedule.count * sizeof(uint16_t));
  prefs.putUChar("win_min", s.schedule.windowMinutes);
  prefs.putUInt("freq_hz", s.freqHz);
}

void settingsForgetWifi() {
  if (!open()) return;
  prefs.remove("wifi_ssid");
  prefs.remove("wifi_pass");
}

uint8_t settingsIncrementBootCount() {
  if (!open()) return 0;
  const uint8_t count = static_cast<uint8_t>(prefs.getUChar("boot_cnt", 0) + 1);
  prefs.putUChar("boot_cnt", count);
  return count;
}

void settingsClearBootCount() {
  if (open()) prefs.putUChar("boot_cnt", 0);
}
