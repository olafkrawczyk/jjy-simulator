#include "provisioning.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <WiFi.h>
#include <esp_system.h>

#include <algorithm>

#include "settings_store.h"

namespace {

const uint8_t kResetPowerUps = 3;
const uint32_t kBootCounterWindowMs = 10000;
const uint32_t kConnectTimeoutMs = 20000;

DNSServer dns;
bool dnsRunning = false;
bool bootCounterPending = false;
std::vector<std::string> networks;
int strongestChannel = 1;
ConnectState connectState = ConnectState::IDLE;
uint32_t connectStartMs = 0;

}  // namespace

bool provisioningResetGestureDetected() {
  if (esp_reset_reason() != ESP_RST_POWERON) return false;
  const uint8_t count = settingsIncrementBootCount();
  Serial.printf("[BOOT] Power-up %u of %u for WiFi reset\n", static_cast<unsigned>(count),
                static_cast<unsigned>(kResetPowerUps));
  if (count >= kResetPowerUps) {
    settingsForgetWifi();
    settingsClearBootCount();
    return true;
  }
  bootCounterPending = true;
  return false;
}

void provisioningBootCounterTick() {
  if (bootCounterPending && millis() > kBootCounterWindowMs) {
    settingsClearBootCount();
    bootCounterPending = false;
  }
}

std::string provisioningApName() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char name[24];
  snprintf(name, sizeof(name), "JJY-Setup-%02X%02X", mac[4], mac[5]);
  return name;
}

void provisioningScan() {
  networks.clear();
  const int found = WiFi.scanNetworks();
  std::vector<int> order;
  for (int i = 0; i < found; ++i) order.push_back(i);
  std::sort(order.begin(), order.end(), [](int a, int b) { return WiFi.RSSI(a) > WiFi.RSSI(b); });
  if (!order.empty()) strongestChannel = WiFi.channel(order[0]);
  for (int i : order) {
    const std::string ssid = WiFi.SSID(i).c_str();
    if (ssid.empty()) continue;
    if (std::find(networks.begin(), networks.end(), ssid) == networks.end()) networks.push_back(ssid);
  }
  WiFi.scanDelete();
  Serial.printf("[SETUP] Found %u networks\n", static_cast<unsigned>(networks.size()));
}

const std::vector<std::string> &provisioningNetworks() { return networks; }

void provisioningStartAp() {
  WiFi.mode(WIFI_AP_STA);
  provisioningScan();
  const std::string name = provisioningApName();
  // Same channel as the strongest network, most likely the home router, so joining it later
  // doesn't move the hotspot and drop the phone.
  WiFi.softAP(name.c_str(), nullptr, strongestChannel);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());
  dnsRunning = true;
  Serial.printf("[SETUP] Hotspot %s on channel %d at %s\n", name.c_str(), strongestChannel,
                WiFi.softAPIP().toString().c_str());
}

void provisioningLoop() {
  if (dnsRunning) dns.processNextRequest();
  if (connectState != ConnectState::CONNECTING) return;
  if (WiFi.status() == WL_CONNECTED) {
    connectState = ConnectState::CONNECTED;
    Serial.printf("[SETUP] Connected, IP %s\n", WiFi.localIP().toString().c_str());
  } else if (millis() - connectStartMs > kConnectTimeoutMs) {
    WiFi.disconnect();
    connectState = ConnectState::FAILED;
    Serial.println("[SETUP] Could not connect.");
  }
}

void provisioningBeginConnect(const std::string &ssid, const std::string &pass) {
  WiFi.disconnect();
  WiFi.begin(ssid.c_str(), pass.empty() ? nullptr : pass.c_str());
  connectState = ConnectState::CONNECTING;
  connectStartMs = millis();
  Serial.printf("[SETUP] Trying %s\n", ssid.c_str());
}

ConnectState provisioningConnectState() { return connectState; }

std::string provisioningStaIp() {
  if (WiFi.status() != WL_CONNECTED) return std::string();
  return std::string(WiFi.localIP().toString().c_str());
}
