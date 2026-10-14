#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>

#include "app_state.h"
#include "carrier.h"
#include "schedule.h"
#include "settings_store.h"
#include "time_sync.h"
#include "transmitter.h"
#include "version.h"

AppState app;

namespace {

const uint32_t kColdBootBroadcastSec = 10 * 60;
const uint32_t kNtpWaitMs = 20000;
const uint32_t kWifiConnectTimeoutMs = 20000;
const uint32_t kLoopPeriodMs = 25;
const uint32_t kFreqStepHz = 10;

bool setupMode = false;
bool coldBootPending = false;
bool stayAwakeLogged = false;

String formatLocal(time_t epoch) {
  struct tm t;
  localtime_r(&epoch, &t);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
  return String(buf);
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("jjy");
  WiFi.setAutoReconnect(true);
  WiFi.begin(app.settings.wifiSsid.c_str(), app.settings.wifiPass.c_str());
  Serial.printf("[WiFi] Connecting to %s", app.settings.wifiSsid.c_str());
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < kWifiConnectTimeoutMs) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] Connected, IP %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[WiFi] Not connected yet; retrying in the background.");
  }
}

void handleSerial() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    uint32_t hz = app.settings.freqHz;
    if (c == '+') {
      hz += kFreqStepHz;
    } else if (c == '-') {
      hz -= kFreqStepHz;
    } else if (c == '?') {
      Serial.printf("[PWM] %lu Hz (range %lu..%lu)\n", static_cast<unsigned long>(hz),
                    static_cast<unsigned long>(kFreqMinHz), static_cast<unsigned long>(kFreqMaxHz));
      continue;
    } else {
      continue;
    }
    if (!settingsValidFreq(hz)) {
      Serial.println("[PWM] Limit reached.");
      continue;
    }
    app.settings.freqHz = hz;
    carrierSetFrequency(hz);
    settingsSave(app.settings);
    Serial.printf("[PWM] %lu Hz\n", static_cast<unsigned long>(hz));
  }
}

bool sleepBlocked() {
  return app.uiHold || app.restartAtMs != 0 || app.otaState == OtaState::INSTALL_REQUESTED ||
         app.otaState == OtaState::INSTALLING;
}

void sleepUntil(time_t now, time_t wake) {
  Serial.printf("[SLEEP] Waking at %s (%ld s)\n", formatLocal(wake).c_str(), static_cast<long>(wake - now));
  Serial.flush();
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(wake - now) * 1000000ULL);
  esp_deep_sleep_start();
}

void runSchedule() {
  if (!timeSyncClockValid()) return;
  const time_t now = time(nullptr);

  if (coldBootPending) {
    coldBootPending = false;
    app.coldBootBroadcast = true;
    txStartWindow(now + kColdBootBroadcastSec);
    Serial.printf("[BOOT] Start-up broadcast until %s\n", formatLocal(now + kColdBootBroadcastSec).c_str());
    return;
  }

  if (txWindowActive()) {
    if (now < txWindowEnd()) return;
    txStopWindow();
    app.coldBootBroadcast = false;
    Serial.printf("[SCHED] Broadcast finished at %s\n", formatLocal(now).c_str());
  }

  time_t start;
  time_t end;
  if (scheduleCurrentWindow(app.settings.schedule, now, start, end)) {
    txStartWindow(end);
    stayAwakeLogged = false;
    Serial.printf("[SCHED] Broadcast %s -> %s\n", formatLocal(start).c_str(), formatLocal(end).c_str());
    return;
  }

  if (sleepBlocked()) return;

  time_t wake;
  if (!scheduleSleepUntil(app.settings.schedule, now, wake)) {
    if (!stayAwakeLogged) {
      Serial.println("[SCHED] Next broadcast is close; staying awake.");
      stayAwakeLogged = true;
    }
    return;
  }
  sleepUntil(now, wake);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\nJJY 40 kHz simulator %s\n", FW_VERSION);
  WiFi.persistent(false);

  settingsLoad(app.settings);
  timeSyncSetTz(app.settings.tzRule.c_str());
  carrierBegin(app.settings.freqHz);
  txBegin();

  if (app.settings.wifiSsid.empty()) {
    setupMode = true;
    Serial.println("[BOOT] No WiFi saved.");
    return;
  }

  const bool timerWake = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
  coldBootPending = !timerWake;
  connectWifi();
  timeSyncBegin(app.settings.tzRule.c_str());
  if (WiFi.status() == WL_CONNECTED && timeSyncWaitForNtp(kNtpWaitMs)) {
    Serial.printf("[NTP] Synced: %s\n", formatLocal(time(nullptr)).c_str());
  } else if (timeSyncClockValid()) {
    Serial.println("[NTP] No answer; using the clock kept during sleep.");
  } else {
    Serial.println("[NTP] No valid time yet; the carrier stays off until it arrives.");
  }
}

void loop() {
  handleSerial();
  if (app.restartAtMs != 0 && static_cast<int32_t>(millis() - app.restartAtMs) >= 0) ESP.restart();
  if (!setupMode) runSchedule();
  delay(kLoopPeriodMs);
}
