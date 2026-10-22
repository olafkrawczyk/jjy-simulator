#pragma once

#include <stdint.h>
#include <time.h>

void txBegin();
void txStartWindow(time_t start, time_t end);
// Moves the end of an active window without restarting the frame; starts a window at `start` if none is active.
void txExtendWindow(time_t start, time_t end);
void txStopWindow();
// Stops the window and waits until the transmitter task confirms the carrier is off.
bool txStopWindowAndWait(uint32_t timeoutMs);
bool txWindowActive();
time_t txWindowStart();
time_t txWindowEnd();
