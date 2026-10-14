#pragma once

#include <stdint.h>

#include <string>
#include <vector>

bool provisioningResetGestureDetected();
void provisioningBootCounterTick();

void provisioningStartAp();
void provisioningLoop();
void provisioningScan();
const std::vector<std::string> &provisioningNetworks();
std::string provisioningApName();

enum class ConnectState : uint8_t { IDLE, CONNECTING, CONNECTED, FAILED };
void provisioningBeginConnect(const std::string &ssid, const std::string &pass);
ConnectState provisioningConnectState();
std::string provisioningStaIp();
