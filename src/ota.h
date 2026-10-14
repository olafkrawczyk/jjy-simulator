#pragma once

#include <string>

struct OtaRelease {
  std::string version;
  std::string url;
};

bool otaFetchLatest(OtaRelease &out, std::string &error);
bool otaInstall(const std::string &url, std::string &error);
void otaMarkRunningImageValid();
bool otaRunningImagePending();
[[noreturn]] void otaRollBack();
