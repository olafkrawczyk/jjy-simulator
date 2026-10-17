#pragma once

#include <stdint.h>

static const uint32_t kCarrierRampMs = 5;

static const uint32_t kCarrierHz = 40000;

void carrierBegin();

// Called only from the transmitter task.
void carrierRampUp();
void carrierRampDown();
void carrierOff();
void carrierTick();
bool carrierIsOn();
