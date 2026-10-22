#include "web_ui.h"

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include "app_state.h"
#include "html.h"
#include "provisioning.h"
#include "settings_rules.h"
#include "settings_store.h"
#include "carrier.h"
#include "schedule.h"
#include "time_sync.h"
#include "transmitter.h"
#include "version.h"
#include "ota.h"
#include "version_compare.h"

namespace {

const uint32_t kRestartAfterSetupMs = 30000;
const uint32_t kBroadcastNowSec = 10 * 60;

WebServer server(80);
bool started = false;
bool setupMode = false;
DeviceSettings pending;
bool pendingValid = false;
DeviceSettings candidate;
bool candidateReady = false;
bool pendingSaved = false;
bool saveFailed = false;

// Host part of "scheme://host[:port]/..." (lower case, no port).
String hostOf(const String &url) {
  const int scheme = url.indexOf("://");
  const int start = scheme >= 0 ? scheme + 3 : 0;
  int end = url.length();
  for (int i = start; i < static_cast<int>(url.length()); ++i) {
    const char c = url[i];
    if (c == '/' || c == ':' || c == '?' || c == '#') {
      end = i;
      break;
    }
  }
  String host = url.substring(start, end);
  host.toLowerCase();
  return host;
}

// Rejects cross-site POSTs: Origin (or else Referer) must match the Host the browser used.
bool originAllowed() {
  String src = server.header("Origin");
  if (src.length() == 0) src = server.header("Referer");
  if (src.length() == 0) return true;
  const String own = server.hostHeader();
  if (own.length() == 0) return true;
  if (hostOf(src) == hostOf(own)) return true;
  server.send(403, "text/plain", "Forbidden");
  return false;
}

const char *kCollectedHeaders[] = {"Origin", "Referer"};

std::string arg(const char *name) { return std::string(server.arg(name).c_str()); }

void sendHtml(const std::string &html) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html; charset=utf-8", String(html.c_str()));
}

void sendJson(const std::string &json) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", String(json.c_str()));
}

bool readTimezone(DeviceSettings &s, std::string &error) {
  s.tzId = arg("tz");
  const std::string custom = arg("tz_custom");
  std::string rule;
  if (settingsResolveTz(s.tzId, custom, rule)) {
    s.tzRule = rule;
    return true;
  }
  if (s.tzId == "custom") {
    s.tzRule = custom;
    error = "That custom time-zone rule doesn't look right.";
  } else {
    error = "Please pick a time zone.";
  }
  return false;
}

bool readSchedule(Schedule &out, std::string &error) {
  Schedule s = Schedule();
  for (int i = 0; i < server.args(); ++i) {
    if (server.argName(i) != "slot") continue;
    String value = server.arg(i);
    value.trim();
    if (value.length() == 0) continue;
    if (s.count >= kMaxSlots) {
      error = scheduleErrorText(ScheduleError::TOO_MANY);
      return false;
    }
    uint16_t minutes = 0;
    if (!parseHHMM(value.c_str(), minutes)) {
      error = scheduleErrorText(ScheduleError::OUT_OF_RANGE);
      return false;
    }
    s.slotMinutes[s.count++] = minutes;
  }
  const long window = server.arg("win").toInt();
  s.windowMinutes = (window > 0 && window <= 255) ? static_cast<uint8_t>(window) : 0;
  scheduleSort(s);
  out = s;
  const ScheduleError result = scheduleValidate(s);
  if (result != ScheduleError::OK) {
    error = scheduleErrorText(result);
    return false;
  }
  return true;
}

SetupPageView setupView(const std::string &error) {
  const DeviceSettings &s = pendingValid ? pending : app.settings;
  SetupPageView v;
  v.networks = provisioningNetworks();
  v.selectedSsid = s.wifiSsid;
  v.tzId = s.tzId;
  v.customTzRule = s.tzId == "custom" ? s.tzRule : "";
  v.schedule = s.schedule;
  v.autoDetectTz = !pendingValid;
  v.error = error;
  return v;
}

void handleSetupPage() {
  if (server.hasArg("rescan") && provisioningConnectState() != ConnectState::CONNECTING) provisioningScan();
  std::string error;
  if (server.hasArg("failed")) {
    error = saveFailed ? "Connected, but the settings couldn't be saved. Please try again."
                       : "Couldn't connect. Check the network name and password, then try again.";
  }
  sendHtml(renderSetupPage(setupView(error)));
}

void handleSetupSubmit() {
  if (!originAllowed()) return;
  DeviceSettings s = app.settings;
  s.wifiSsid = arg("ssid");
  if (s.wifiSsid.empty()) s.wifiSsid = arg("ssid_other");
  s.wifiPass = arg("pass");
  std::string wifiError;
  std::string tzError;
  std::string scheduleError;
  const bool wifiOk = settingsValidWifi(s.wifiSsid, s.wifiPass, wifiError);
  const bool tzOk = readTimezone(s, tzError);
  const bool scheduleOk = readSchedule(s.schedule, scheduleError);
  pending = s;
  pendingValid = true;
  if (!wifiOk || !tzOk || !scheduleOk) {
    sendHtml(renderSetupPage(setupView(!wifiOk ? wifiError : (!tzOk ? tzError : scheduleError))));
    return;
  }
  candidate = s;
  candidateReady = true;
  pendingSaved = false;
  saveFailed = false;
  provisioningBeginConnect(s.wifiSsid, s.wifiPass);
  sendHtml(renderConnectingPage(provisioningHostname()));
}

void handleSetupStatus() {
  const ConnectState state = provisioningConnectState();
  const char *name = saveFailed                            ? "failed"
                     : state == ConnectState::CONNECTED ? "connected"
                     : state == ConnectState::FAILED  ? "failed"
                                                      : "connecting";
  std::string json = "{\"state\":\"";
  json += name;
  json += "\",\"ip\":\"";
  json += jsonEscape(provisioningStaIp());
  json += "\"}";
  sendJson(json);
}

void handleCaptiveRedirect() {
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "");
}

std::string formatLocalHHMM(time_t epoch) {
  struct tm t;
  localtime_r(&epoch, &t);
  char buf[8];
  strftime(buf, sizeof(buf), "%H:%M", &t);
  return buf;
}

void markUiActivity() {
  if (app.unscheduledBroadcast && !app.uiHold) {
    app.uiHold = true;
    Serial.println("[WEB] Page opened during an unscheduled broadcast; sleep paused until Save.");
  }
  if (app.uiHold) app.uiHoldLastMs = millis();
}

SettingsPageView settingsView(const DeviceSettings &s, const std::string &notice, const std::string &error) {
  SettingsPageView v;
  if (timeSyncClockValid()) {
    const time_t now = time(nullptr);
    v.localTime = formatLocalHHMM(now);
    const time_t next = scheduleNextStart(app.settings.schedule, now);
    if (next != 0) v.nextBroadcast = formatLocalHHMM(next);
  }
  v.broadcasting = txWindowActive();
  if (v.broadcasting && timeSyncClockValid()) v.broadcastUntil = formatLocalHHMM(txWindowEnd());
  v.tzId = s.tzId;
  v.customTzRule = s.tzId == "custom" ? s.tzRule : "";
  v.schedule = s.schedule;
  v.hostname = provisioningHostname();
  v.version = FW_VERSION;
  v.updateVersion = app.latestVersion;
  v.notice = notice;
  v.error = error;
  return v;
}

void handleSettingsPage() {
  markUiActivity();
  sendHtml(renderSettingsPage(settingsView(app.settings, "", "")));
}

void handleSave() {
  if (!originAllowed()) return;
  DeviceSettings s = app.settings;
  std::string tzError;
  std::string scheduleError;
  std::string error;
  const bool tzOk = readTimezone(s, tzError);
  const bool scheduleOk = readSchedule(s.schedule, scheduleError);
  bool ok = tzOk && scheduleOk;
  if (!ok) error = !tzOk ? tzError : scheduleError;
  if (ok && !settingsSave(s)) {
    ok = false;
    error = "Couldn't save the settings. Please try again.";
  }
  if (!ok) {
    sendHtml(renderSettingsPage(settingsView(s, "", error)));
    return;
  }

  const bool timingChanged = s.tzRule != app.settings.tzRule || !scheduleEquals(s.schedule, app.settings.schedule);
  app.settings = s;
  timeSyncSetTz(s.tzRule.c_str());
  if (timingChanged && txWindowActive() && !app.unscheduledBroadcast) txStopWindow();
  app.uiHold = false;
  app.sleepNotBeforeMs = millis() + 5000;
  if (app.sleepNotBeforeMs == 0) app.sleepNotBeforeMs = 1;

  SettingsPageView v = settingsView(app.settings, "", "");
  v.notice = "Saved. ";
  if (!v.nextBroadcast.empty()) v.notice += "Next broadcast at " + v.nextBroadcast + ". ";
  v.notice += "The transmitter sleeps between broadcasts. To open this page again, unplug it and plug it back in.";
  sendHtml(renderSettingsPage(v));
}

void handleBroadcastNow() {
  if (!originAllowed()) return;
  if (!timeSyncClockValid()) {
    sendHtml(renderSettingsPage(
        settingsView(app.settings, "", "The transmitter doesn't know the time yet. Try again in a minute.")));
    return;
  }
  if (app.otaState == OtaState::INSTALL_REQUESTED || app.otaState == OtaState::INSTALLING) {
    sendHtml(renderSettingsPage(settingsView(app.settings, "", "An update is being installed. Try again later.")));
    return;
  }
  const time_t end = time(nullptr) + kBroadcastNowSec;
  if (!txWindowActive() || txWindowEnd() < end) {
    txExtendWindow(end);
    app.unscheduledBroadcast = true;
    Serial.printf("[WEB] Broadcast now until %s\n", formatLocalHHMM(end).c_str());
  }
  markUiActivity();
  SettingsPageView v = settingsView(app.settings, "", "");
  v.notice = "Broadcasting until " + v.broadcastUntil + ". Start reception on your clock now.";
  sendHtml(renderSettingsPage(v));
}

void handleForget() {
  if (!originAllowed()) return;
  if (!settingsForgetWifi()) {
    sendHtml(renderSettingsPage(settingsView(app.settings, "", "Couldn't forget the WiFi network. Please try again.")));
    return;
  }
  app.settings.wifiSsid.clear();
  app.settings.wifiPass.clear();
  app.restartAtMs = millis() + 3000;
  sendHtml(renderMessagePage("WiFi forgotten", "The transmitter is restarting in setup mode. On your phone, join "
                                               "the WiFi network " +
                                                   provisioningApName() + " to set it up again."));
}

void handleStatus() {
  markUiActivity();
  const char *ota = app.otaState == OtaState::FAILED ? "failed" : (app.otaState == OtaState::IDLE ? "idle" : "updating");
  std::string json = "{\"version\":\"";
  json += jsonEscape(FW_VERSION);
  json += "\",\"time\":\"";
  if (timeSyncClockValid()) json += formatLocalHHMM(time(nullptr));
  json += "\",\"broadcasting\":";
  json += txWindowActive() ? "true" : "false";
  json += ",\"ota\":\"";
  json += ota;
  json += "\",\"error\":\"";
  json += jsonEscape(app.otaError);
  json += "\"}";
  sendJson(json);
}

void handleUpdateCheck() {
  if (!originAllowed()) return;
  markUiActivity();
  OtaRelease release;
  std::string error;
  if (!otaFetchLatest(release, error)) {
    sendHtml(renderSettingsPage(settingsView(app.settings, "", error)));
    return;
  }
  if (versionCompare(release.version.c_str(), FW_VERSION) > 0) {
    app.latestVersion = release.version;
    app.latestUrl = release.url;
    sendHtml(renderSettingsPage(settingsView(app.settings, "Version " + release.version + " is available.", "")));
    return;
  }
  app.latestVersion.clear();
  app.latestUrl.clear();
  sendHtml(renderSettingsPage(settingsView(app.settings, "You're up to date.", "")));
}

void handleUpdateInstall() {
  if (!originAllowed()) return;
  markUiActivity();
  if (app.latestUrl.empty()) {
    sendHtml(renderSettingsPage(settingsView(app.settings, "", "Check for updates first.")));
    return;
  }
  app.otaState = OtaState::INSTALL_REQUESTED;
  app.otaError.clear();
  sendHtml(renderUpdatingPage(FW_VERSION));
}

}  // namespace

void webUiBeginSetup() {
  setupMode = true;
  server.on("/", HTTP_GET, handleSetupPage);
  server.on("/setup", HTTP_POST, handleSetupSubmit);
  server.on("/setup/status", HTTP_GET, handleSetupStatus);
  server.onNotFound(handleCaptiveRedirect);
  server.collectHeaders(kCollectedHeaders, 2);
  server.begin();
  started = true;
  Serial.println("[WEB] Setup page ready");
}

void webUiBeginNormal() {
  setupMode = false;
  server.on("/", HTTP_GET, handleSettingsPage);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/broadcast", HTTP_POST, handleBroadcastNow);
  server.on("/forget", HTTP_POST, handleForget);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/update/check", HTTP_POST, handleUpdateCheck);
  server.on("/update/install", HTTP_POST, handleUpdateInstall);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.collectHeaders(kCollectedHeaders, 2);
  server.begin();
  started = true;
  Serial.println("[WEB] Settings page ready");
}

void webUiLoop() {
  if (!started) return;
  server.handleClient();
  if (setupMode && candidateReady && !pendingSaved && provisioningConnectState() == ConnectState::CONNECTED) {
    pendingSaved = true;
    if (settingsSave(candidate)) {
      app.restartAtMs = millis() + kRestartAfterSetupMs;
      Serial.println("[SETUP] Settings saved; restarting in 30 s.");
    } else {
      saveFailed = true;
      Serial.println("[SETUP] Could not save settings.");
    }
  }
}
