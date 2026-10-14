#pragma once

#include <stdint.h>
#include <time.h>

void txBegin();
void txStartWindow(time_t end);
void txStopWindow();
// Stops the window and waits until the transmitter task confirms the carrier is off.
bool txStopWindowAndWait(uint32_t timeoutMs);
bool txWindowActive();
time_t txWindowEnd();
