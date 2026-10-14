#pragma once

#include <stdint.h>

static const uint32_t kCarrierRampMs = 5;

void carrierBegin(uint32_t frequencyHz);
// Safe from any task; the transmitter task applies it on its next tick.
void carrierSetFrequency(uint32_t frequencyHz);
uint32_t carrierFrequency();

// Called only from the transmitter task.
void carrierRampUp();
void carrierRampDown();
void carrierOff();
void carrierTick();
bool carrierIsOn();
