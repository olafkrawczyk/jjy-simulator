#include "jjy_frame.h"

namespace {

void setBit(JjySymbol out[], int index, int value) {
  out[index] = value ? JjySymbol::ONE : JjySymbol::ZERO;
}

int countBits(int value) {
  int n = 0;
  while (value) {
    n += value & 1;
    value >>= 1;
  }
  return n;
}

}  // namespace

void jjyBuildFrame(const struct tm &t, JjySymbol out[kJjyFrameLength]) {
  for (int i = 0; i < kJjyFrameLength; ++i) out[i] = JjySymbol::ZERO;
  const int markers[] = {0, 9, 19, 29, 39, 49, 59};
  for (int m : markers) out[m] = JjySymbol::MARKER;

  const int minTens = t.tm_min / 10;
  const int minOnes = t.tm_min % 10;
  const int hourTens = t.tm_hour / 10;
  const int hourOnes = t.tm_hour % 10;
  const int yday = t.tm_yday + 1;
  const int ydayHundreds = yday / 100;
  const int ydayTens = (yday / 10) % 10;
  const int ydayOnes = yday % 10;
  const int year2 = (t.tm_year + 1900) % 100;
  const int yearTens = year2 / 10;
  const int yearOnes = year2 % 10;

  setBit(out, 1, minTens & 0x4);
  setBit(out, 2, minTens & 0x2);
  setBit(out, 3, minTens & 0x1);
  setBit(out, 5, minOnes & 0x8);
  setBit(out, 6, minOnes & 0x4);
  setBit(out, 7, minOnes & 0x2);
  setBit(out, 8, minOnes & 0x1);

  setBit(out, 12, hourTens & 0x2);
  setBit(out, 13, hourTens & 0x1);
  setBit(out, 15, hourOnes & 0x8);
  setBit(out, 16, hourOnes & 0x4);
  setBit(out, 17, hourOnes & 0x2);
  setBit(out, 18, hourOnes & 0x1);

  setBit(out, 22, ydayHundreds & 0x2);
  setBit(out, 23, ydayHundreds & 0x1);
  setBit(out, 25, ydayTens & 0x8);
  setBit(out, 26, ydayTens & 0x4);
  setBit(out, 27, ydayTens & 0x2);
  setBit(out, 28, ydayTens & 0x1);
  setBit(out, 30, ydayOnes & 0x8);
  setBit(out, 31, ydayOnes & 0x4);
  setBit(out, 32, ydayOnes & 0x2);
  setBit(out, 33, ydayOnes & 0x1);

  setBit(out, 36, (countBits(hourTens & 0x3) + countBits(hourOnes & 0xF)) & 1);
  setBit(out, 37, (countBits(minTens & 0x7) + countBits(minOnes & 0xF)) & 1);

  setBit(out, 41, yearTens & 0x8);
  setBit(out, 42, yearTens & 0x4);
  setBit(out, 43, yearTens & 0x2);
  setBit(out, 44, yearTens & 0x1);
  setBit(out, 45, yearOnes & 0x8);
  setBit(out, 46, yearOnes & 0x4);
  setBit(out, 47, yearOnes & 0x2);
  setBit(out, 48, yearOnes & 0x1);

  setBit(out, 50, t.tm_wday & 0x4);
  setBit(out, 51, t.tm_wday & 0x2);
  setBit(out, 52, t.tm_wday & 0x1);
}

uint16_t jjyOnDurationMs(JjySymbol symbol) {
  switch (symbol) {
    case JjySymbol::MARKER:
      return 200;
    case JjySymbol::ONE:
      return 500;
    default:
      return 800;
  }
}

char jjySymbolChar(JjySymbol symbol) {
  switch (symbol) {
    case JjySymbol::MARKER:
      return 'P';
    case JjySymbol::ONE:
      return '1';
    default:
      return '0';
  }
}
