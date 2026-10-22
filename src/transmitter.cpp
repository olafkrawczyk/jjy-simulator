#include "transmitter.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "carrier.h"
#include "jjy_frame.h"
#include "time_sync.h"

namespace {

const BaseType_t kCore = 1;
const UBaseType_t kPriority = 4;
const uint32_t kStackBytes = 4096;

portMUX_TYPE windowMux = portMUX_INITIALIZER_UNLOCKED;
bool windowActive = false;
time_t windowEnd = 0;
uint32_t windowGeneration = 0;
volatile uint32_t acknowledgedGeneration = 0;

void txTask(void *) {
  JjySymbol frame[kJjyFrameLength];
  int frameMinute = -1;
  int frameYday = -1;
  int frameYear = -1;
  int lastSecond = -1;
  uint32_t seenGeneration = 0;
  bool pulseActive = false;
  bool rampDownStarted = false;
  uint32_t pulseStartMs = 0;
  uint16_t pulseMs = 0;
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    carrierTick();

    bool active;
    uint32_t generation;
    portENTER_CRITICAL(&windowMux);
    active = windowActive;
    generation = windowGeneration;
    portEXIT_CRITICAL(&windowMux);

    if (generation != seenGeneration) {
      seenGeneration = generation;
      lastSecond = -1;
      if (timeSyncClockValid()) {
        const time_t t = time(nullptr);
        struct tm startLocal;
        localtime_r(&t, &startLocal);
        lastSecond = startLocal.tm_sec;  // first pulse starts on the next second boundary
      }
      frameMinute = -1;
      pulseActive = false;
      carrierOff();
      acknowledgedGeneration = generation;
    }

    if (!active || !timeSyncClockValid()) {
      if (pulseActive || carrierIsOn()) {
        carrierOff();
        pulseActive = false;
      }
      vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(1));
      continue;
    }

    const time_t now = time(nullptr);
    struct tm local;
    localtime_r(&now, &local);

    if (local.tm_min != frameMinute || local.tm_yday != frameYday || local.tm_year != frameYear) {
      jjyBuildFrame(local, frame);
      frameMinute = local.tm_min;
      frameYday = local.tm_yday;
      frameYear = local.tm_year;
      Serial.printf("[FRAME] %04d day %03d %02d:%02d\n", local.tm_year + 1900, local.tm_yday + 1, local.tm_hour,
                    local.tm_min);
    }

    if (local.tm_sec != lastSecond && local.tm_sec < kJjyFrameLength) {
      lastSecond = local.tm_sec;
      const JjySymbol symbol = frame[lastSecond];
      pulseMs = jjyOnDurationMs(symbol);
      pulseStartMs = millis();
      pulseActive = true;
      rampDownStarted = false;
      carrierOff();
      carrierRampUp();
      Serial.printf("[TX] sec=%02d %c\n", lastSecond, jjySymbolChar(symbol));
    }

    if (pulseActive) {
      const uint32_t elapsed = millis() - pulseStartMs;
      if (!rampDownStarted && elapsed + kCarrierRampMs >= pulseMs) {
        carrierRampDown();
        rampDownStarted = true;
      }
      if (elapsed >= pulseMs) {
        carrierOff();
        pulseActive = false;
      }
    }

    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(1));
  }
}

}  // namespace

void txBegin() { xTaskCreatePinnedToCore(txTask, "jjy_tx", kStackBytes, nullptr, kPriority, nullptr, kCore); }

void txStartWindow(time_t end) {
  portENTER_CRITICAL(&windowMux);
  windowActive = true;
  windowEnd = end;
  ++windowGeneration;
  portEXIT_CRITICAL(&windowMux);
}

void txExtendWindow(time_t end) {
  portENTER_CRITICAL(&windowMux);
  if (!windowActive) {
    windowActive = true;
    ++windowGeneration;
  }
  windowEnd = end;
  portEXIT_CRITICAL(&windowMux);
}

bool txStopWindowAndWait(uint32_t timeoutMs) {
  uint32_t generation;
  portENTER_CRITICAL(&windowMux);
  windowActive = false;
  generation = ++windowGeneration;
  portEXIT_CRITICAL(&windowMux);
  const uint32_t start = millis();
  while (acknowledgedGeneration != generation) {
    if (millis() - start >= timeoutMs) return false;
    delay(1);
  }
  return true;
}

void txStopWindow() {
  portENTER_CRITICAL(&windowMux);
  windowActive = false;
  ++windowGeneration;
  portEXIT_CRITICAL(&windowMux);
}

bool txWindowActive() {
  portENTER_CRITICAL(&windowMux);
  const bool active = windowActive;
  portEXIT_CRITICAL(&windowMux);
  return active;
}

time_t txWindowEnd() {
  portENTER_CRITICAL(&windowMux);
  const time_t end = windowEnd;
  portEXIT_CRITICAL(&windowMux);
  return end;
}
