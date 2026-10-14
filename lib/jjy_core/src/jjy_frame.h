#pragma once

#include <stdint.h>
#include <time.h>

enum class JjySymbol : uint8_t { ZERO, ONE, MARKER };

static const int kJjyFrameLength = 60;

void jjyBuildFrame(const struct tm &localTime, JjySymbol out[kJjyFrameLength]);
uint16_t jjyOnDurationMs(JjySymbol symbol);
char jjySymbolChar(JjySymbol symbol);
