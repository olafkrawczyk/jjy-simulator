#include "ota.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>

#include "certs.h"

namespace {
const char kLatestReleaseUrl[] = "https://api.github.com/repos/olafkrawczyk/jjy-simulator/releases/latest";
}

bool otaFetchLatest(OtaRelease &out, std::string &error) {
  WiFiClientSecure client;
  client.setCACert(kRootCerts);
  client.setHandshakeTimeout(15);
  HTTPClient http;
  http.setTimeout(15000);
  http.useHTTP10(true);  // no chunked encoding, so ArduinoJson can read the stream directly
  if (!http.begin(client, kLatestReleaseUrl)) {
    error = "Couldn't check for updates. Try again later.";
    return false;
  }
  http.setUserAgent("jjy-simulator");
  http.addHeader("Accept", "application/vnd.github+json");
  const int code = http.GET();
  if (code == HTTP_CODE_NOT_FOUND) {
    http.end();
    error = "No updates have been published yet.";
    return false;
  }
  if (code != HTTP_CODE_OK) {
    Serial.printf("[OTA] GitHub answered %d\n", code);
    http.end();
    error = "Couldn't check for updates. Try again later.";
    return false;
  }

  JsonDocument filter;
  filter["tag_name"] = true;
  filter["assets"][0]["name"] = true;
  filter["assets"][0]["browser_download_url"] = true;
  JsonDocument doc;
  const DeserializationError parseError =
      deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (parseError) {
    Serial.printf("[OTA] Parse error: %s\n", parseError.c_str());
    error = "Couldn't check for updates. Try again later.";
    return false;
  }

  const char *tag = doc["tag_name"] | "";
  out.url.clear();
  for (JsonObject asset : doc["assets"].as<JsonArray>()) {
    if (strcmp(asset["name"] | "", "firmware.bin") == 0) out.url = asset["browser_download_url"] | "";
  }
  if (*tag == '\0' || out.url.empty()) {
    error = "The latest release has no firmware file.";
    return false;
  }
  out.version = (tag[0] == 'v' || tag[0] == 'V') ? tag + 1 : tag;
  Serial.printf("[OTA] Latest release %s\n", out.version.c_str());
  return true;
}

bool otaInstall(const std::string &url, std::string &error) {
  WiFiClientSecure client;
  client.setCACert(kRootCerts);
  client.setHandshakeTimeout(15);
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  httpUpdate.rebootOnUpdate(false);
  const t_httpUpdate_return result = httpUpdate.update(client, String(url.c_str()));
  if (result == HTTP_UPDATE_OK) return true;
  Serial.printf("[OTA] Update failed: %s\n", httpUpdate.getLastErrorString().c_str());
  error = "The update didn't finish. The current version is still installed. Try again later.";
  return false;
}

void otaMarkRunningImageValid() {
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
      state == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_ota_mark_app_valid_cancel_rollback();
    Serial.println("[OTA] New firmware confirmed.");
  }
}

bool otaRunningImagePending() {
  esp_ota_img_states_t state;
  return esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
         state == ESP_OTA_IMG_PENDING_VERIFY;
}

void otaRollBack() {
  Serial.println("[OTA] New firmware never reached the network; rolling back.");
  Serial.flush();
  esp_ota_mark_app_invalid_rollback_and_reboot();
  for (;;) delay(1000);
}
