#include "carrier.h"

#include <Arduino.h>
#include <driver/mcpwm.h>

namespace {

const int kPinA = 25;
const int kPinB = 26;
#if defined(LED_BUILTIN)
const int kPinLed = LED_BUILTIN;
#else
const int kPinLed = 2;
#endif
const mcpwm_unit_t kUnit = MCPWM_UNIT_0;
const mcpwm_timer_t kTimer = MCPWM_TIMER_0;
const uint32_t kApbClockHz = 80000000;
const uint32_t kDeadtimeNs = 150;
const float kDutyTarget = 45.0f;
const uint8_t kRampSteps = 5;
const uint32_t kRampStepUs = kCarrierRampMs * 1000 / (kRampSteps - 1);
const float kRampDuty[kRampSteps] = {0.0f, kDutyTarget * 0.25f, kDutyTarget * 0.50f, kDutyTarget * 0.75f,
                                     kDutyTarget};

enum class Ramp : uint8_t { NONE, UP, DOWN };

volatile uint32_t requestedHz = 0;
uint32_t activeHz = 0;
float activeDuty = 0.0f;
Ramp ramp = Ramp::NONE;
uint8_t rampIndex = 0;
uint32_t rampStepStartUs = 0;

void applyDuty(float duty) {
  if (duty <= 0.0f) {
    mcpwm_set_duty_type(kUnit, kTimer, MCPWM_OPR_A, MCPWM_DUTY_MODE_0);
    mcpwm_set_duty_type(kUnit, kTimer, MCPWM_OPR_B, MCPWM_DUTY_MODE_0);
    mcpwm_set_duty(kUnit, kTimer, MCPWM_OPR_A, 0.0f);
    mcpwm_set_duty(kUnit, kTimer, MCPWM_OPR_B, 0.0f);
    digitalWrite(kPinLed, LOW);
    activeDuty = 0.0f;
    return;
  }
  mcpwm_set_duty_type(kUnit, kTimer, MCPWM_OPR_A, MCPWM_DUTY_MODE_0);
  mcpwm_set_duty_type(kUnit, kTimer, MCPWM_OPR_B, MCPWM_DUTY_MODE_1);
  mcpwm_set_duty(kUnit, kTimer, MCPWM_OPR_A, duty);
  mcpwm_set_duty(kUnit, kTimer, MCPWM_OPR_B, duty);
  digitalWrite(kPinLed, HIGH);
  activeDuty = duty;
}

uint32_t deadtimeTicks() {
  const uint64_t ticks = (static_cast<uint64_t>(kDeadtimeNs) * kApbClockHz + 999999999ULL) / 1000000000ULL;
  return ticks == 0 ? 1 : static_cast<uint32_t>(ticks);
}

}  // namespace

void carrierBegin(uint32_t frequencyHz) {
  pinMode(kPinLed, OUTPUT);
  digitalWrite(kPinLed, LOW);
  mcpwm_gpio_init(kUnit, MCPWM0A, kPinA);
  mcpwm_gpio_init(kUnit, MCPWM0B, kPinB);
  mcpwm_config_t cfg = {};
  cfg.frequency = frequencyHz;
  cfg.cmpr_a = 0.0f;
  cfg.cmpr_b = 0.0f;
  cfg.counter_mode = MCPWM_UP_COUNTER;
  cfg.duty_mode = MCPWM_DUTY_MODE_0;
  mcpwm_init(kUnit, kTimer, &cfg);
  const uint32_t dt = deadtimeTicks();
  const esp_err_t dtErr = mcpwm_deadtime_enable(kUnit, kTimer, MCPWM_ACTIVE_HIGH_MODE, dt, dt);
  if (dtErr != ESP_OK) Serial.printf("[CARRIER] Dead-time enable failed: err=%d\n", static_cast<int>(dtErr));
  activeHz = frequencyHz;
  requestedHz = frequencyHz;
  applyDuty(0.0f);
  Serial.printf("[CARRIER] GPIO%d/GPIO%d at %lu Hz, dead time %lu ticks\n", kPinA, kPinB,
                static_cast<unsigned long>(frequencyHz), static_cast<unsigned long>(dt));
}

void carrierSetFrequency(uint32_t frequencyHz) { requestedHz = frequencyHz; }

uint32_t carrierFrequency() { return requestedHz; }

void carrierRampUp() {
  ramp = Ramp::UP;
  rampIndex = 0;
  rampStepStartUs = micros();
  applyDuty(kRampDuty[rampIndex]);
}

void carrierRampDown() {
  ramp = Ramp::DOWN;
  rampIndex = kRampSteps - 1;
  rampStepStartUs = micros();
  applyDuty(kRampDuty[rampIndex]);
}

void carrierOff() {
  ramp = Ramp::NONE;
  applyDuty(0.0f);
}

bool carrierIsOn() { return activeDuty > 0.0f; }

void carrierTick() {
  const uint32_t hz = requestedHz;
  if (hz != activeHz) {
    mcpwm_set_frequency(kUnit, kTimer, hz);
    activeHz = hz;
    applyDuty(activeDuty);
  }
  while (ramp != Ramp::NONE && micros() - rampStepStartUs >= kRampStepUs) {
    rampStepStartUs += kRampStepUs;
    if (ramp == Ramp::UP) {
      ++rampIndex;
      applyDuty(kRampDuty[rampIndex]);
      if (rampIndex == kRampSteps - 1) ramp = Ramp::NONE;
    } else {
      --rampIndex;
      applyDuty(kRampDuty[rampIndex]);
      if (rampIndex == 0) ramp = Ramp::NONE;
    }
  }
}
