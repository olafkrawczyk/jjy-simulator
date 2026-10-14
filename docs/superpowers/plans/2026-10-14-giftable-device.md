# Giftable JJY Device Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a non-technical person set up, configure and update the ESP32 JJY transmitter from a phone browser, with no app and no buttons.

**Architecture:**
- **Pure logic** lives in a PlatformIO library, `lib/jjy_core`, with no Arduino includes, and is unit-tested on the PC. It covers the JJY frame, schedule maths, time-zone table, settings validation, version comparison and HTML rendering.
- **Hardware and network code** lives in focused files under `src/`: carrier, transmitter task, time sync, settings storage, provisioning, web UI and OTA.
- **`main.cpp` only wires them together.** The web server is used only in `src/web_ui.cpp`, so a later move to ESPAsyncWebServer stays contained.

**Tech Stack:**
- ESP32 Arduino core 2.0.17, which needs C++11 and uses gcc 8.4.
- PlatformIO with `espressif32 @ 6.12.0`.
- Built-in `WebServer`, `DNSServer`, `ESPmDNS`, `Preferences`, `WiFiClientSecure`, `HTTPClient` and `HTTPUpdate`.
- ArduinoJson 7.
- Unity on the PlatformIO `native` platform for PC tests.
- GitHub Actions for releases.

**Spec:** `docs/superpowers/specs/2026-10-14-giftable-device-design.md`

## Global Constraints

### Toolchain and code rules
- The board is `esp32doit-devkit-v1` on `espressif32 @ 6.12.0`. Code in `lib/jjy_core` must compile as C++11 on both gcc 8.4 (ESP32) and host gcc.
- `lib/jjy_core` must not include `Arduino.h` or any ESP-IDF header.
- PC tests run on Linux (WSL or CI): `~/.venvs/pio/bin/pio test -e native`. Firmware builds use the Windows PlatformIO: `/mnt/c/Users/olafk/.platformio/penv/Scripts/pio.exe run -e esp32doit-devkit-v1`.
- Only `src/web_ui.cpp` may include `WebServer.h`.

### Web pages
- Pages make no external requests. They use one inline stylesheet and system fonts, with no CDN.
- Anything that changes state is a POST. GET only renders.
- Every dynamic value is escaped with `htmlEscape` or `jsonEscape`.

### Storage and defaults
- The NVS namespace is `"jjy"`. Keys: `wifi_ssid`, `wifi_pass`, `tz_id`, `tz_rule`, `slots`, `win_min`, `freq_hz`, `boot_cnt`. `web_ovrd` is removed on load.
- Default schedule: 01:16, 04:16, 13:16 and 16:16, stored as minutes `76, 256, 796, 976`. Window length 5 minutes, allowed range 1–30, up to 8 start times.
- Default time zone: id `warsaw`, rule `CET-1CEST,M3.5.0/2,M10.5.0/3`.
- Carrier frequency: 39500–40500 Hz, default 40000.

### Network and updates
- The setup hotspot is open and named `JJY-Setup-XXXX`, where XXXX is the last two MAC bytes in uppercase hex. The mDNS name is `jjy`, so the settings page is at `http://jjy.local`.
- Updates come from `https://api.github.com/repos/olafkrawczyk/jjy-simulator/releases/latest`, using the release asset named `firmware.bin`.
- `FW_VERSION` defaults to `"dev"`. CI sets it from the tag without the leading `v`.

### Timing
- The device wakes 120 s before each window. It stays awake instead of sleeping when the wake time is less than 30 s away.
- A cold boot runs a 10-minute broadcast.
- Three quick power-ups within 10 s reset the WiFi settings.

### Deviations from the spec, decided while planning
- **Custom time-zone rule:** the box appears when you pick "Custom rule (advanced)" in the time-zone list, on both pages, instead of under "Advanced". `tzset()` can't report errors, so custom rules are checked by shape: 3–64 printable characters with no spaces, starting with a letter or `<`.
- **Late start:** a scheduled window that is already under way starts transmitting immediately, for whatever remains of it. The old firmware skipped it if it was more than 1 s late.
- **Periodic NTP resync:** removed. SNTP repeats its own sync every hour by default.
- **Bootloader rollback:** confirmed available. `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` is set in the core's sdkconfig, and the core has a weak `verifyRollbackLater()` hook.

---

## File Structure

```
platformio.ini                         modify: pin platform, partitions, native env, ArduinoJson
.gitignore                             modify: drop include/secrets.h line
include/secrets.h.example              delete
lib/jjy_core/library.json              create
lib/jjy_core/src/jjy_frame.{h,cpp}     create: 60-symbol JJY frame
lib/jjy_core/src/timezones.{h,cpp}     create: city table
lib/jjy_core/src/schedule.{h,cpp}      create: windows, next start, sleep plan, validation
lib/jjy_core/src/settings_rules.{h,cpp} create: DeviceSettings, defaults, validation, HH:MM
lib/jjy_core/src/version_compare.{h,cpp} create: semver compare
lib/jjy_core/src/html.{h,cpp}          create: escaping + page rendering
test/test_jjy_frame/test_main.cpp      create
test/test_timezones/test_main.cpp      create
test/test_schedule/test_main.cpp       create
test/test_settings_rules/test_main.cpp create
test/test_html/test_main.cpp           create
src/version.h                          create: FW_VERSION default
src/app_state.h                        create: state shared by main and web_ui
src/carrier.{h,cpp}                    create: MCPWM, ramp, LED (single owner)
src/transmitter.{h,cpp}                create: 1 ms task, window state behind a lock
src/time_sync.{h,cpp}                  create: TZ + SNTP
src/settings_store.{h,cpp}             create: NVS load/save/migrate, boot counter
src/provisioning.{h,cpp}               create: hotspot, DNS, scan, connect attempt, reset gesture
src/web_ui.{h,cpp}                     create: all HTTP routes
src/ota.{h,cpp}                        create: GitHub check + install, rollback marking
src/certs.h                            create (generated): root CAs
tools/make_certs_header.sh             create
src/main.cpp                           rewrite: wiring + schedule controller
.github/workflows/release.yml          create
README.md                              modify: setup/reset/update/release sections
```

---

### Task 1: PC test setup and the JJY frame module

**Files:**
- Modify: `platformio.ini`
- Create: `lib/jjy_core/library.json`, `lib/jjy_core/src/jjy_frame.h`, `lib/jjy_core/src/jjy_frame.cpp`
- Test: `test/test_jjy_frame/test_main.cpp`

**Interfaces:**
- Produces:
  - `enum class JjySymbol : uint8_t { ZERO, ONE, MARKER };`
  - `static const int kJjyFrameLength = 60;`
  - `void jjyBuildFrame(const struct tm &localTime, JjySymbol out[kJjyFrameLength]);`
  - `uint16_t jjyOnDurationMs(JjySymbol symbol);` returns 200 for a marker, 500 for a one and 800 for a zero.
  - `char jjySymbolChar(JjySymbol symbol);` returns `'P'`, `'1'` or `'0'`.

- [ ] **Step 1: Install PlatformIO in WSL for the PC tests**

Run: `python3 -m venv ~/.venvs/pio && ~/.venvs/pio/bin/pip install -q platformio && ~/.venvs/pio/bin/pio --version`
Expected: `PlatformIO Core, version 6.x.x`

- [ ] **Step 2: Replace `platformio.ini`**

```ini
[platformio]
default_envs = esp32doit-devkit-v1

[env:esp32doit-devkit-v1]
platform = espressif32 @ 6.12.0
board = esp32doit-devkit-v1
framework = arduino
monitor_speed = 115200
board_build.partitions = default.csv

[env:native]
platform = native
test_framework = unity
build_flags = -std=gnu++11 -Wall
```

- [ ] **Step 3: Create `lib/jjy_core/library.json`**

```json
{
  "name": "jjy_core",
  "version": "1.0.0",
  "description": "Hardware-independent JJY simulator logic",
  "frameworks": "*",
  "platforms": "*"
}
```

- [ ] **Step 4: Write the failing test `test/test_jjy_frame/test_main.cpp`**

```cpp
#include <unity.h>

#include <string>

#include "jjy_frame.h"

void setUp() {}
void tearDown() {}

static struct tm makeTm(int year, int yday0, int wday, int hour, int minute) {
  struct tm t = {};
  t.tm_year = year - 1900;
  t.tm_yday = yday0;
  t.tm_wday = wday;
  t.tm_hour = hour;
  t.tm_min = minute;
  return t;
}

static std::string frameString(const struct tm &t) {
  JjySymbol frame[kJjyFrameLength];
  jjyBuildFrame(t, frame);
  std::string s;
  for (int i = 0; i < kJjyFrameLength; ++i) s += jjySymbolChar(frame[i]);
  return s;
}

// 2026-10-14 13:16, day 287 (tm_yday 286), Wednesday.
// min 16 -> 001 0110, hour 13 -> 01 0011, day 287 -> 10 1000 0111,
// PA1 = parity(hour bits) = 1, PA2 = parity(minute bits) = 1, year 26 -> 0010 0110, weekday 3 -> 011.
void test_reference_frame() {
  const char *expected =
      "P00100110P"
      "000100011P"
      "001001000P"
      "011100110P"
      "000100110P"
      "011000000P";
  TEST_ASSERT_EQUAL_STRING(expected, frameString(makeTm(2026, 286, 3, 13, 16)).c_str());
}

void test_markers_at_fixed_positions() {
  const std::string s = frameString(makeTm(2025, 0, 3, 0, 0));
  const int markers[] = {0, 9, 19, 29, 39, 49, 59};
  for (int m : markers) TEST_ASSERT_EQUAL_CHAR('P', s[m]);
}

// 2024-12-31 23:59 is day 366: hundreds 3 -> bits 22,23 = 1,1; tens 6 -> 25..28 = 0110; ones 6 -> 30..33 = 0110.
void test_day_366() {
  const std::string s = frameString(makeTm(2024, 365, 2, 23, 59));
  TEST_ASSERT_EQUAL_STRING("11", s.substr(22, 2).c_str());
  TEST_ASSERT_EQUAL_STRING("0110", s.substr(25, 4).c_str());
  TEST_ASSERT_EQUAL_STRING("0110", s.substr(30, 4).c_str());
}

void test_on_durations() {
  TEST_ASSERT_EQUAL_UINT16(200, jjyOnDurationMs(JjySymbol::MARKER));
  TEST_ASSERT_EQUAL_UINT16(500, jjyOnDurationMs(JjySymbol::ONE));
  TEST_ASSERT_EQUAL_UINT16(800, jjyOnDurationMs(JjySymbol::ZERO));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_reference_frame);
  RUN_TEST(test_markers_at_fixed_positions);
  RUN_TEST(test_day_366);
  RUN_TEST(test_on_durations);
  return UNITY_END();
}
```

- [ ] **Step 5: Run it to make sure it fails**

Run: `~/.venvs/pio/bin/pio test -e native -f test_jjy_frame`
Expected: FAIL. The compile error is `jjy_frame.h: No such file or directory`.

- [ ] **Step 6: Create `lib/jjy_core/src/jjy_frame.h`**

```cpp
#pragma once

#include <stdint.h>
#include <time.h>

enum class JjySymbol : uint8_t { ZERO, ONE, MARKER };

static const int kJjyFrameLength = 60;

void jjyBuildFrame(const struct tm &localTime, JjySymbol out[kJjyFrameLength]);
uint16_t jjyOnDurationMs(JjySymbol symbol);
char jjySymbolChar(JjySymbol symbol);
```

- [ ] **Step 7: Create `lib/jjy_core/src/jjy_frame.cpp`** (logic ported from the old `buildJjyFrame`)

```cpp
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
```

- [ ] **Step 8: Run the test to verify it passes**

Run: `~/.venvs/pio/bin/pio test -e native -f test_jjy_frame`
Expected: `4 Tests 0 Failures 0 Ignored`, ending in `PASSED`.

- [ ] **Step 9: Check the firmware still builds**

Run: `/mnt/c/Users/olafk/.platformio/penv/Scripts/pio.exe run -e esp32doit-devkit-v1`
Expected: `[SUCCESS]`. The library is not used by firmware yet.

- [ ] **Step 10: Commit**

```bash
git add platformio.ini lib/jjy_core test/test_jjy_frame
git commit -m "Add jjy_core library with tested JJY frame builder"
```

---

### Task 2: Time-zone table

**Files:**
- Create: `lib/jjy_core/src/timezones.h`, `lib/jjy_core/src/timezones.cpp`
- Test: `test/test_timezones/test_main.cpp`

**Interfaces:**
- Produces:
  - `struct TzEntry { const char *id; const char *label; const char *iana; const char *posix; };`. `iana` may hold several space-separated aliases.
  - `extern const TzEntry kTimezones[];` and `extern const size_t kTimezoneCount;`
  - `const TzEntry *tzFindById(const char *id);` returns `nullptr` if the id is unknown.

- [ ] **Step 1: Write the failing test `test/test_timezones/test_main.cpp`**

```cpp
#include <unity.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "timezones.h"

void setUp() {}
void tearDown() {}

struct Expected {
  const char *id;
  long january;
  long july;
};

// UTC offsets in seconds at 2026-01-15 12:00 UTC and 2026-07-15 12:00 UTC.
static const Expected kExpected[] = {
    {"honolulu", -36000, -36000},   {"anchorage", -32400, -28800},  {"los_angeles", -28800, -25200},
    {"denver", -25200, -21600},     {"phoenix", -25200, -25200},    {"chicago", -21600, -18000},
    {"mexico_city", -21600, -21600}, {"new_york", -18000, -14400},  {"halifax", -14400, -10800},
    {"sao_paulo", -10800, -10800},  {"buenos_aires", -10800, -10800}, {"reykjavik", 0, 0},
    {"london", 0, 3600},            {"dublin", 0, 3600},            {"lisbon", 0, 3600},
    {"madrid", 3600, 7200},         {"paris", 3600, 7200},          {"amsterdam", 3600, 7200},
    {"berlin", 3600, 7200},         {"rome", 3600, 7200},           {"stockholm", 3600, 7200},
    {"prague", 3600, 7200},         {"warsaw", 3600, 7200},         {"lagos", 3600, 3600},
    {"athens", 7200, 10800},        {"bucharest", 7200, 10800},     {"helsinki", 7200, 10800},
    {"kyiv", 7200, 10800},          {"cairo", 7200, 10800},         {"johannesburg", 7200, 7200},
    {"istanbul", 10800, 10800},     {"moscow", 10800, 10800},       {"nairobi", 10800, 10800},
    {"dubai", 14400, 14400},        {"karachi", 18000, 18000},      {"delhi", 19800, 19800},
    {"dhaka", 21600, 21600},        {"bangkok", 25200, 25200},      {"jakarta", 25200, 25200},
    {"singapore", 28800, 28800},    {"hong_kong", 28800, 28800},    {"shanghai", 28800, 28800},
    {"taipei", 28800, 28800},       {"manila", 28800, 28800},       {"perth", 28800, 28800},
    {"seoul", 32400, 32400},        {"tokyo", 32400, 32400},        {"adelaide", 37800, 34200},
    {"brisbane", 36000, 36000},     {"sydney", 39600, 36000},       {"auckland", 46800, 43200},
    {"utc", 0, 0},
};

static long offsetAt(const char *posix, time_t epoch) {
  setenv("TZ", posix, 1);
  tzset();
  struct tm t;
  localtime_r(&epoch, &t);
  return t.tm_gmtoff;
}

void test_every_entry_has_expected_offsets() {
  const time_t january = 1768478400;  // 2026-01-15 12:00:00 UTC
  const time_t july = 1784116800;     // 2026-07-15 12:00:00 UTC
  const size_t expectedCount = sizeof(kExpected) / sizeof(kExpected[0]);
  TEST_ASSERT_EQUAL_UINT32(expectedCount, kTimezoneCount);
  for (size_t i = 0; i < kTimezoneCount; ++i) {
    const TzEntry &e = kTimezones[i];
    const Expected *found = nullptr;
    for (size_t j = 0; j < expectedCount; ++j) {
      if (strcmp(kExpected[j].id, e.id) == 0) found = &kExpected[j];
    }
    TEST_ASSERT_NOT_NULL_MESSAGE(found, e.id);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(found->january, offsetAt(e.posix, january), e.id);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(found->july, offsetAt(e.posix, july), e.id);
  }
}

void test_ids_unique_and_labels_present() {
  for (size_t i = 0; i < kTimezoneCount; ++i) {
    TEST_ASSERT_TRUE(strlen(kTimezones[i].label) > 0);
    TEST_ASSERT_TRUE(strlen(kTimezones[i].iana) > 0);
    for (size_t j = i + 1; j < kTimezoneCount; ++j) {
      TEST_ASSERT_TRUE_MESSAGE(strcmp(kTimezones[i].id, kTimezones[j].id) != 0, kTimezones[i].id);
    }
  }
}

void test_find_by_id() {
  TEST_ASSERT_EQUAL_STRING("JST-9", tzFindById("tokyo")->posix);
  TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0/2,M10.5.0/3", tzFindById("warsaw")->posix);
  TEST_ASSERT_NULL(tzFindById("atlantis"));
  TEST_ASSERT_NULL(tzFindById("custom"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_every_entry_has_expected_offsets);
  RUN_TEST(test_ids_unique_and_labels_present);
  RUN_TEST(test_find_by_id);
  return UNITY_END();
}
```

- [ ] **Step 2: Run it to make sure it fails**

Run: `~/.venvs/pio/bin/pio test -e native -f test_timezones`
Expected: FAIL with `timezones.h: No such file or directory`.

- [ ] **Step 3: Create `lib/jjy_core/src/timezones.h`**

```cpp
#pragma once

#include <stddef.h>

struct TzEntry {
  const char *id;
  const char *label;
  const char *iana;  // space-separated IANA names the browser may report
  const char *posix;
};

extern const TzEntry kTimezones[];
extern const size_t kTimezoneCount;

const TzEntry *tzFindById(const char *id);
```

- [ ] **Step 4: Create `lib/jjy_core/src/timezones.cpp`**

```cpp
#include "timezones.h"

#include <string.h>

#define CET_RULE "CET-1CEST,M3.5.0,M10.5.0/3"
#define EET_RULE "EET-2EEST,M3.5.0/3,M10.5.0/4"

const TzEntry kTimezones[] = {
    {"honolulu", "Honolulu", "Pacific/Honolulu", "HST10"},
    {"anchorage", "Anchorage", "America/Anchorage", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"los_angeles", "Los Angeles / Vancouver", "America/Los_Angeles America/Vancouver", "PST8PDT,M3.2.0,M11.1.0"},
    {"denver", "Denver / Edmonton", "America/Denver America/Edmonton", "MST7MDT,M3.2.0,M11.1.0"},
    {"phoenix", "Phoenix", "America/Phoenix", "MST7"},
    {"chicago", "Chicago / Winnipeg", "America/Chicago America/Winnipeg", "CST6CDT,M3.2.0,M11.1.0"},
    {"mexico_city", "Mexico City", "America/Mexico_City", "CST6"},
    {"new_york", "New York / Toronto", "America/New_York America/Toronto", "EST5EDT,M3.2.0,M11.1.0"},
    {"halifax", "Halifax", "America/Halifax", "AST4ADT,M3.2.0,M11.1.0"},
    {"sao_paulo", "S\xC3\xA3o Paulo", "America/Sao_Paulo", "<-03>3"},
    {"buenos_aires", "Buenos Aires", "America/Argentina/Buenos_Aires America/Buenos_Aires", "<-03>3"},
    {"reykjavik", "Reykjavik", "Atlantic/Reykjavik", "GMT0"},
    {"london", "London", "Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"dublin", "Dublin", "Europe/Dublin", "GMT0IST,M3.5.0/1,M10.5.0"},
    {"lisbon", "Lisbon", "Europe/Lisbon", "WET0WEST,M3.5.0/1,M10.5.0"},
    {"madrid", "Madrid", "Europe/Madrid", CET_RULE},
    {"paris", "Paris", "Europe/Paris", CET_RULE},
    {"amsterdam", "Amsterdam / Brussels", "Europe/Amsterdam Europe/Brussels", CET_RULE},
    {"berlin", "Berlin", "Europe/Berlin", CET_RULE},
    {"rome", "Rome", "Europe/Rome", CET_RULE},
    {"stockholm", "Stockholm / Oslo / Copenhagen", "Europe/Stockholm Europe/Oslo Europe/Copenhagen", CET_RULE},
    {"prague", "Prague / Vienna", "Europe/Prague Europe/Vienna", CET_RULE},
    {"warsaw", "Warsaw", "Europe/Warsaw", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {"lagos", "Lagos", "Africa/Lagos", "WAT-1"},
    {"athens", "Athens", "Europe/Athens", EET_RULE},
    {"bucharest", "Bucharest", "Europe/Bucharest", EET_RULE},
    {"helsinki", "Helsinki", "Europe/Helsinki", EET_RULE},
    {"kyiv", "Kyiv", "Europe/Kyiv Europe/Kiev", EET_RULE},
    {"cairo", "Cairo", "Africa/Cairo", "EET-2EEST,M4.5.5/0,M10.5.4/24"},
    {"johannesburg", "Johannesburg", "Africa/Johannesburg", "SAST-2"},
    {"istanbul", "Istanbul", "Europe/Istanbul", "<+03>-3"},
    {"moscow", "Moscow", "Europe/Moscow", "MSK-3"},
    {"nairobi", "Nairobi", "Africa/Nairobi", "EAT-3"},
    {"dubai", "Dubai", "Asia/Dubai", "<+04>-4"},
    {"karachi", "Karachi", "Asia/Karachi", "PKT-5"},
    {"delhi", "Delhi / Mumbai", "Asia/Kolkata Asia/Calcutta", "IST-5:30"},
    {"dhaka", "Dhaka", "Asia/Dhaka", "<+06>-6"},
    {"bangkok", "Bangkok / Hanoi", "Asia/Bangkok Asia/Ho_Chi_Minh Asia/Saigon", "<+07>-7"},
    {"jakarta", "Jakarta", "Asia/Jakarta", "WIB-7"},
    {"singapore", "Singapore / Kuala Lumpur", "Asia/Singapore Asia/Kuala_Lumpur", "<+08>-8"},
    {"hong_kong", "Hong Kong", "Asia/Hong_Kong", "HKT-8"},
    {"shanghai", "Beijing / Shanghai", "Asia/Shanghai", "CST-8"},
    {"taipei", "Taipei", "Asia/Taipei", "CST-8"},
    {"manila", "Manila", "Asia/Manila", "PST-8"},
    {"perth", "Perth", "Australia/Perth", "AWST-8"},
    {"seoul", "Seoul", "Asia/Seoul", "KST-9"},
    {"tokyo", "Tokyo", "Asia/Tokyo", "JST-9"},
    {"adelaide", "Adelaide", "Australia/Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
    {"brisbane", "Brisbane", "Australia/Brisbane", "AEST-10"},
    {"sydney", "Sydney / Melbourne", "Australia/Sydney Australia/Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"auckland", "Auckland", "Pacific/Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    {"utc", "UTC (no daylight saving)", "UTC Etc/UTC", "UTC0"},
};

const size_t kTimezoneCount = sizeof(kTimezones) / sizeof(kTimezones[0]);

const TzEntry *tzFindById(const char *id) {
  for (size_t i = 0; i < kTimezoneCount; ++i) {
    if (strcmp(kTimezones[i].id, id) == 0) return &kTimezones[i];
  }
  return nullptr;
}
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `~/.venvs/pio/bin/pio test -e native -f test_timezones`
Expected: `3 Tests 0 Failures`, ending in `PASSED`.

- [ ] **Step 6: Commit**

```bash
git add lib/jjy_core/src/timezones.h lib/jjy_core/src/timezones.cpp test/test_timezones
git commit -m "Add city time-zone table with offset tests"
```

---

### Task 3: Schedule module

**Files:**
- Create: `lib/jjy_core/src/schedule.h`, `lib/jjy_core/src/schedule.cpp`
- Test: `test/test_schedule/test_main.cpp`

**Interfaces:**
- Produces, all using the process time zone that the caller has already set:
  - `struct Schedule { uint16_t slotMinutes[kMaxSlots]; uint8_t count; uint8_t windowMinutes; };`
  - constants `kMaxSlots = 8`, `kMinutesPerDay = 1440`, `kWindowMinMinutes = 1`, `kWindowMaxMinutes = 30`, `kWakeLeadSec = 120`, `kMinSleepSec = 30`
  - `enum class ScheduleError : uint8_t { OK, EMPTY, TOO_MANY, OUT_OF_RANGE, DUPLICATE, WINDOW_LENGTH, OVERLAP };`
  - `void scheduleSort(Schedule &s);`
  - `ScheduleError scheduleValidate(const Schedule &s);` validates a sorted copy, so the input may be unsorted.
  - `const char *scheduleErrorText(ScheduleError e);`
  - `bool scheduleEquals(const Schedule &a, const Schedule &b);`
  - `bool scheduleCurrentWindow(const Schedule &s, time_t now, time_t &startOut, time_t &endOut);` The window end is exclusive.
  - `time_t scheduleNextStart(const Schedule &s, time_t now);` returns the first start strictly after `now`, or 0 if there is none.
  - `bool scheduleSleepUntil(const Schedule &s, time_t now, time_t &wakeOut);` A false result means stay awake.

- [ ] **Step 1: Write the failing test `test/test_schedule/test_main.cpp`**

```cpp
#include <unity.h>

#include <stdlib.h>
#include <time.h>

#include <initializer_list>

#include "schedule.h"

void setUp() {
  setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
  tzset();
}
void tearDown() {}

static time_t local(int y, int mo, int d, int h, int mi, int s = 0) {
  struct tm t = {};
  t.tm_year = y - 1900;
  t.tm_mon = mo - 1;
  t.tm_mday = d;
  t.tm_hour = h;
  t.tm_min = mi;
  t.tm_sec = s;
  t.tm_isdst = -1;
  return mktime(&t);
}

static struct tm parts(time_t epoch) {
  struct tm t;
  localtime_r(&epoch, &t);
  return t;
}

static Schedule make(std::initializer_list<uint16_t> slots, uint8_t windowMinutes) {
  Schedule s = Schedule();
  for (uint16_t m : slots) s.slotMinutes[s.count++] = m;
  s.windowMinutes = windowMinutes;
  return s;
}

static Schedule defaults() { return make({76, 256, 796, 976}, 5); }

void test_validate_defaults_ok() { TEST_ASSERT_EQUAL(ScheduleError::OK, scheduleValidate(defaults())); }

void test_validate_errors() {
  TEST_ASSERT_EQUAL(ScheduleError::EMPTY, scheduleValidate(make({}, 5)));
  Schedule tooMany = defaults();
  tooMany.count = 9;
  TEST_ASSERT_EQUAL(ScheduleError::TOO_MANY, scheduleValidate(tooMany));
  TEST_ASSERT_EQUAL(ScheduleError::OUT_OF_RANGE, scheduleValidate(make({1440}, 5)));
  TEST_ASSERT_EQUAL(ScheduleError::DUPLICATE, scheduleValidate(make({600, 600}, 5)));
  TEST_ASSERT_EQUAL(ScheduleError::WINDOW_LENGTH, scheduleValidate(make({600}, 0)));
  TEST_ASSERT_EQUAL(ScheduleError::WINDOW_LENGTH, scheduleValidate(make({600}, 31)));
  TEST_ASSERT_EQUAL(ScheduleError::OVERLAP, scheduleValidate(make({600, 603}, 5)));
  TEST_ASSERT_EQUAL(ScheduleError::OVERLAP, scheduleValidate(make({1438, 1}, 5)));
}

void test_validate_accepts_unsorted_and_touching() {
  TEST_ASSERT_EQUAL(ScheduleError::OK, scheduleValidate(make({600, 60}, 5)));
  TEST_ASSERT_EQUAL(ScheduleError::OK, scheduleValidate(make({600, 605}, 5)));
}

void test_sort_and_equals() {
  Schedule s = make({796, 76}, 5);
  scheduleSort(s);
  TEST_ASSERT_EQUAL_UINT16(76, s.slotMinutes[0]);
  TEST_ASSERT_TRUE(scheduleEquals(s, make({76, 796}, 5)));
  TEST_ASSERT_FALSE(scheduleEquals(s, make({76, 796}, 6)));
  TEST_ASSERT_FALSE(scheduleEquals(s, make({76}, 5)));
}

void test_current_window_inside_and_edges() {
  time_t start, end;
  TEST_ASSERT_TRUE(scheduleCurrentWindow(defaults(), local(2026, 10, 14, 13, 18), start, end));
  TEST_ASSERT_EQUAL_INT64(local(2026, 10, 14, 13, 16), start);
  TEST_ASSERT_EQUAL_INT64(local(2026, 10, 14, 13, 21), end);
  TEST_ASSERT_TRUE(scheduleCurrentWindow(defaults(), local(2026, 10, 14, 13, 16), start, end));
  TEST_ASSERT_FALSE(scheduleCurrentWindow(defaults(), local(2026, 10, 14, 13, 21), start, end));
  TEST_ASSERT_FALSE(scheduleCurrentWindow(defaults(), local(2026, 10, 14, 12, 0), start, end));
}

void test_current_window_across_midnight() {
  time_t start, end;
  TEST_ASSERT_TRUE(scheduleCurrentWindow(make({1438}, 5), local(2026, 10, 15, 0, 1), start, end));
  TEST_ASSERT_EQUAL_INT64(local(2026, 10, 14, 23, 58), start);
  TEST_ASSERT_EQUAL_INT64(local(2026, 10, 15, 0, 3), end);
}

void test_next_start() {
  TEST_ASSERT_EQUAL_INT64(local(2026, 10, 14, 16, 16), scheduleNextStart(defaults(), local(2026, 10, 14, 13, 16)));
  TEST_ASSERT_EQUAL_INT64(local(2026, 10, 15, 1, 16), scheduleNextStart(defaults(), local(2026, 10, 14, 17, 0)));
}

void test_dst_spring_forward_missing_time() {
  // 2026-03-29 02:00 CET jumps to 03:00 CEST; 02:30 does not exist and becomes 03:30.
  const time_t next = scheduleNextStart(make({150}, 5), local(2026, 3, 29, 0, 0));
  const struct tm t = parts(next);
  TEST_ASSERT_EQUAL_INT(29, t.tm_mday);
  TEST_ASSERT_EQUAL_INT(3, t.tm_hour);
  TEST_ASSERT_EQUAL_INT(30, t.tm_min);
}

void test_dst_fall_back_runs_once() {
  // 2026-10-25 03:00 CEST falls back to 02:00 CET; 02:30 happens twice but must broadcast once.
  const Schedule s = make({150}, 5);
  const time_t first = scheduleNextStart(s, local(2026, 10, 25, 0, 0));
  TEST_ASSERT_EQUAL_INT(25, parts(first).tm_mday);
  const time_t second = scheduleNextStart(s, first);
  TEST_ASSERT_EQUAL_INT(26, parts(second).tm_mday);
}

void test_window_length_on_dst_day() {
  time_t start, end;
  TEST_ASSERT_TRUE(scheduleCurrentWindow(defaults(), local(2026, 3, 29, 1, 17), start, end));
  TEST_ASSERT_EQUAL_INT64(300, end - start);
}

void test_sleep_until() {
  time_t wake = 0;
  TEST_ASSERT_TRUE(scheduleSleepUntil(defaults(), local(2026, 10, 14, 12, 0), wake));
  TEST_ASSERT_EQUAL_INT64(local(2026, 10, 14, 13, 14), wake);
  TEST_ASSERT_FALSE(scheduleSleepUntil(defaults(), local(2026, 10, 14, 13, 15), wake));
  TEST_ASSERT_FALSE(scheduleSleepUntil(defaults(), local(2026, 10, 14, 13, 13, 40), wake));
}

void test_close_windows_stay_awake() {
  // Window 10:00-10:01 ends; the next starts at 10:02 so the wake time (10:00) has passed.
  time_t wake = 0;
  TEST_ASSERT_FALSE(scheduleSleepUntil(make({600, 602}, 1), local(2026, 10, 14, 10, 1), wake));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_validate_defaults_ok);
  RUN_TEST(test_validate_errors);
  RUN_TEST(test_validate_accepts_unsorted_and_touching);
  RUN_TEST(test_sort_and_equals);
  RUN_TEST(test_current_window_inside_and_edges);
  RUN_TEST(test_current_window_across_midnight);
  RUN_TEST(test_next_start);
  RUN_TEST(test_dst_spring_forward_missing_time);
  RUN_TEST(test_dst_fall_back_runs_once);
  RUN_TEST(test_window_length_on_dst_day);
  RUN_TEST(test_sleep_until);
  RUN_TEST(test_close_windows_stay_awake);
  return UNITY_END();
}
```

- [ ] **Step 2: Run it to make sure it fails**

Run: `~/.venvs/pio/bin/pio test -e native -f test_schedule`
Expected: FAIL with `schedule.h: No such file or directory`.

- [ ] **Step 3: Create `lib/jjy_core/src/schedule.h`**

```cpp
#pragma once

#include <stdint.h>
#include <time.h>

static const uint8_t kMaxSlots = 8;
static const uint16_t kMinutesPerDay = 1440;
static const uint8_t kWindowMinMinutes = 1;
static const uint8_t kWindowMaxMinutes = 30;
static const uint32_t kWakeLeadSec = 120;
static const uint32_t kMinSleepSec = 30;

struct Schedule {
  uint16_t slotMinutes[kMaxSlots];
  uint8_t count;
  uint8_t windowMinutes;
};

enum class ScheduleError : uint8_t { OK, EMPTY, TOO_MANY, OUT_OF_RANGE, DUPLICATE, WINDOW_LENGTH, OVERLAP };

void scheduleSort(Schedule &s);
ScheduleError scheduleValidate(const Schedule &s);
const char *scheduleErrorText(ScheduleError e);
bool scheduleEquals(const Schedule &a, const Schedule &b);

// The functions below interpret slot times in the current TZ.
bool scheduleCurrentWindow(const Schedule &s, time_t now, time_t &startOut, time_t &endOut);
time_t scheduleNextStart(const Schedule &s, time_t now);
bool scheduleSleepUntil(const Schedule &s, time_t now, time_t &wakeOut);
```

- [ ] **Step 4: Create `lib/jjy_core/src/schedule.cpp`**

```cpp
#include "schedule.h"

#include <algorithm>

namespace {

time_t slotStart(const struct tm &base, uint16_t minutes, int dayOffset) {
  struct tm t = base;
  t.tm_mday += dayOffset;
  t.tm_hour = minutes / 60;
  t.tm_min = minutes % 60;
  t.tm_sec = 0;
  t.tm_isdst = -1;
  return mktime(&t);
}

}  // namespace

void scheduleSort(Schedule &s) {
  if (s.count > kMaxSlots) return;
  std::sort(s.slotMinutes, s.slotMinutes + s.count);
}

ScheduleError scheduleValidate(const Schedule &input) {
  if (input.count == 0) return ScheduleError::EMPTY;
  if (input.count > kMaxSlots) return ScheduleError::TOO_MANY;
  if (input.windowMinutes < kWindowMinMinutes || input.windowMinutes > kWindowMaxMinutes) {
    return ScheduleError::WINDOW_LENGTH;
  }
  Schedule s = input;
  scheduleSort(s);
  for (uint8_t i = 0; i < s.count; ++i) {
    if (s.slotMinutes[i] >= kMinutesPerDay) return ScheduleError::OUT_OF_RANGE;
  }
  for (uint8_t i = 1; i < s.count; ++i) {
    if (s.slotMinutes[i] == s.slotMinutes[i - 1]) return ScheduleError::DUPLICATE;
    if (s.slotMinutes[i] - s.slotMinutes[i - 1] < s.windowMinutes) return ScheduleError::OVERLAP;
  }
  if (s.count > 1) {
    const int wrapGap = s.slotMinutes[0] + kMinutesPerDay - s.slotMinutes[s.count - 1];
    if (wrapGap < s.windowMinutes) return ScheduleError::OVERLAP;
  }
  return ScheduleError::OK;
}

const char *scheduleErrorText(ScheduleError e) {
  switch (e) {
    case ScheduleError::OK:
      return "";
    case ScheduleError::EMPTY:
      return "Add at least one start time.";
    case ScheduleError::TOO_MANY:
      return "You can add up to 8 start times.";
    case ScheduleError::OUT_OF_RANGE:
      return "Please enter times like 01:16.";
    case ScheduleError::DUPLICATE:
      return "Each start time can only appear once.";
    case ScheduleError::WINDOW_LENGTH:
      return "Broadcast length must be between 1 and 30 minutes.";
    case ScheduleError::OVERLAP:
      return "Broadcasts would overlap. Space the start times further apart or shorten the broadcast.";
  }
  return "";
}

bool scheduleEquals(const Schedule &a, const Schedule &b) {
  if (a.count != b.count || a.windowMinutes != b.windowMinutes) return false;
  for (uint8_t i = 0; i < a.count && i < kMaxSlots; ++i) {
    if (a.slotMinutes[i] != b.slotMinutes[i]) return false;
  }
  return true;
}

bool scheduleCurrentWindow(const Schedule &s, time_t now, time_t &startOut, time_t &endOut) {
  struct tm base;
  localtime_r(&now, &base);
  for (int day = -1; day <= 0; ++day) {
    for (uint8_t i = 0; i < s.count && i < kMaxSlots; ++i) {
      const time_t start = slotStart(base, s.slotMinutes[i], day);
      const time_t end = start + static_cast<time_t>(s.windowMinutes) * 60;
      if (now >= start && now < end) {
        startOut = start;
        endOut = end;
        return true;
      }
    }
  }
  return false;
}

time_t scheduleNextStart(const Schedule &s, time_t now) {
  struct tm base;
  localtime_r(&now, &base);
  time_t best = 0;
  for (int day = 0; day <= 1; ++day) {
    for (uint8_t i = 0; i < s.count && i < kMaxSlots; ++i) {
      const time_t start = slotStart(base, s.slotMinutes[i], day);
      if (start > now && (best == 0 || start < best)) best = start;
    }
  }
  return best;
}

bool scheduleSleepUntil(const Schedule &s, time_t now, time_t &wakeOut) {
  const time_t next = scheduleNextStart(s, now);
  if (next == 0) return false;
  const time_t wake = next - static_cast<time_t>(kWakeLeadSec);
  if (wake - now < static_cast<time_t>(kMinSleepSec)) return false;
  wakeOut = wake;
  return true;
}
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `~/.venvs/pio/bin/pio test -e native -f test_schedule`
Expected: `12 Tests 0 Failures`, ending in `PASSED`.

- [ ] **Step 6: Commit**

```bash
git add lib/jjy_core/src/schedule.h lib/jjy_core/src/schedule.cpp test/test_schedule
git commit -m "Add schedule module: windows, next start, sleep plan, validation"
```

---

### Task 4: Settings rules and version comparison

**Files:**
- Create: `lib/jjy_core/src/settings_rules.h`, `lib/jjy_core/src/settings_rules.cpp`, `lib/jjy_core/src/version_compare.h`, `lib/jjy_core/src/version_compare.cpp`
- Test: `test/test_settings_rules/test_main.cpp`

**Interfaces:**
- Consumes: `Schedule`, `scheduleValidate` (Task 3); `tzFindById` (Task 2).
- Produces:
  - `kFreqMinHz = 39500`, `kFreqMaxHz = 40500`, `kFreqDefaultHz = 40000`
  - `struct DeviceSettings { std::string wifiSsid, wifiPass, tzId, tzRule; Schedule schedule; uint32_t freqHz; };`
  - `DeviceSettings settingsDefaults();`
  - `bool settingsValidWifi(const std::string &ssid, const std::string &pass, std::string &error);`
  - `bool settingsValidTzRule(const std::string &rule);`
  - `bool settingsResolveTz(const std::string &id, const std::string &customRule, std::string &ruleOut);`
  - `bool settingsValidFreq(uint32_t hz);`
  - `bool parseHHMM(const std::string &text, uint16_t &minutesOut);`
  - `std::string formatHHMM(uint16_t minutes);`
  - `int versionCompare(const char *a, const char *b);` returns -1, 0 or 1. `"dev"` and anything unparsable sort lowest.

- [ ] **Step 1: Write the failing test `test/test_settings_rules/test_main.cpp`**

```cpp
#include <unity.h>

#include <string>

#include "settings_rules.h"
#include "version_compare.h"

void setUp() {}
void tearDown() {}

void test_defaults() {
  const DeviceSettings s = settingsDefaults();
  TEST_ASSERT_EQUAL_STRING("warsaw", s.tzId.c_str());
  TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0/2,M10.5.0/3", s.tzRule.c_str());
  TEST_ASSERT_EQUAL_UINT8(4, s.schedule.count);
  TEST_ASSERT_EQUAL_UINT16(76, s.schedule.slotMinutes[0]);
  TEST_ASSERT_EQUAL_UINT16(976, s.schedule.slotMinutes[3]);
  TEST_ASSERT_EQUAL_UINT8(5, s.schedule.windowMinutes);
  TEST_ASSERT_EQUAL_UINT32(40000, s.freqHz);
  TEST_ASSERT_TRUE(s.wifiSsid.empty());
  TEST_ASSERT_EQUAL(ScheduleError::OK, scheduleValidate(s.schedule));
}

void test_wifi_rules() {
  std::string error;
  TEST_ASSERT_FALSE(settingsValidWifi("", "", error));
  TEST_ASSERT_FALSE(error.empty());
  TEST_ASSERT_FALSE(settingsValidWifi(std::string(33, 'a'), "", error));
  TEST_ASSERT_TRUE(settingsValidWifi(std::string(32, 'a'), "", error));
  TEST_ASSERT_FALSE(settingsValidWifi("Home", "1234567", error));
  TEST_ASSERT_TRUE(settingsValidWifi("Home", "12345678", error));
  TEST_ASSERT_TRUE(settingsValidWifi("Home", std::string(63, 'p'), error));
  TEST_ASSERT_FALSE(settingsValidWifi("Home", std::string(64, 'p'), error));
}

void test_tz_rule_shape() {
  TEST_ASSERT_TRUE(settingsValidTzRule("JST-9"));
  TEST_ASSERT_TRUE(settingsValidTzRule("<+03>-3"));
  TEST_ASSERT_FALSE(settingsValidTzRule(""));
  TEST_ASSERT_FALSE(settingsValidTzRule("has space"));
  TEST_ASSERT_FALSE(settingsValidTzRule("9abc"));
  TEST_ASSERT_FALSE(settingsValidTzRule(std::string(65, 'A')));
}

void test_resolve_tz() {
  std::string rule;
  TEST_ASSERT_TRUE(settingsResolveTz("tokyo", "", rule));
  TEST_ASSERT_EQUAL_STRING("JST-9", rule.c_str());
  TEST_ASSERT_TRUE(settingsResolveTz("custom", "UTC0", rule));
  TEST_ASSERT_EQUAL_STRING("UTC0", rule.c_str());
  TEST_ASSERT_FALSE(settingsResolveTz("custom", "bad rule", rule));
  TEST_ASSERT_FALSE(settingsResolveTz("atlantis", "", rule));
}

void test_freq() {
  TEST_ASSERT_FALSE(settingsValidFreq(39490));
  TEST_ASSERT_TRUE(settingsValidFreq(39500));
  TEST_ASSERT_TRUE(settingsValidFreq(40500));
  TEST_ASSERT_FALSE(settingsValidFreq(40510));
}

void test_parse_and_format_hhmm() {
  uint16_t m = 0;
  TEST_ASSERT_TRUE(parseHHMM("01:16", m));
  TEST_ASSERT_EQUAL_UINT16(76, m);
  TEST_ASSERT_TRUE(parseHHMM("1:16", m));
  TEST_ASSERT_EQUAL_UINT16(76, m);
  TEST_ASSERT_TRUE(parseHHMM("23:59", m));
  TEST_ASSERT_EQUAL_UINT16(1439, m);
  TEST_ASSERT_TRUE(parseHHMM("00:00", m));
  TEST_ASSERT_EQUAL_UINT16(0, m);
  TEST_ASSERT_TRUE(parseHHMM("01:16:30", m));
  TEST_ASSERT_EQUAL_UINT16(76, m);
  TEST_ASSERT_FALSE(parseHHMM("24:00", m));
  TEST_ASSERT_FALSE(parseHHMM("01:60", m));
  TEST_ASSERT_FALSE(parseHHMM("01:6", m));
  TEST_ASSERT_FALSE(parseHHMM("abc", m));
  TEST_ASSERT_FALSE(parseHHMM("", m));
  TEST_ASSERT_EQUAL_STRING("01:16", formatHHMM(76).c_str());
  TEST_ASSERT_EQUAL_STRING("00:00", formatHHMM(0).c_str());
  TEST_ASSERT_EQUAL_STRING("23:59", formatHHMM(1439).c_str());
}

void test_version_compare() {
  TEST_ASSERT_EQUAL_INT(1, versionCompare("1.2.0", "1.1.9"));
  TEST_ASSERT_EQUAL_INT(0, versionCompare("v1.2.0", "1.2.0"));
  TEST_ASSERT_EQUAL_INT(1, versionCompare("1.10.0", "1.9.0"));
  TEST_ASSERT_EQUAL_INT(1, versionCompare("2", "1.9.9"));
  TEST_ASSERT_EQUAL_INT(1, versionCompare("1.0.0", "dev"));
  TEST_ASSERT_EQUAL_INT(-1, versionCompare("dev", "1.0.0"));
  TEST_ASSERT_EQUAL_INT(0, versionCompare("dev", "dev"));
  TEST_ASSERT_EQUAL_INT(0, versionCompare("1.0.0-rc1", "1.0.0"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults);
  RUN_TEST(test_wifi_rules);
  RUN_TEST(test_tz_rule_shape);
  RUN_TEST(test_resolve_tz);
  RUN_TEST(test_freq);
  RUN_TEST(test_parse_and_format_hhmm);
  RUN_TEST(test_version_compare);
  return UNITY_END();
}
```

- [ ] **Step 2: Run it to make sure it fails**

Run: `~/.venvs/pio/bin/pio test -e native -f test_settings_rules`
Expected: FAIL with `settings_rules.h: No such file or directory`.

- [ ] **Step 3: Create `lib/jjy_core/src/settings_rules.h`**

```cpp
#pragma once

#include <stdint.h>

#include <string>

#include "schedule.h"

static const uint32_t kFreqMinHz = 39500;
static const uint32_t kFreqMaxHz = 40500;
static const uint32_t kFreqDefaultHz = 40000;

struct DeviceSettings {
  std::string wifiSsid;
  std::string wifiPass;
  std::string tzId;
  std::string tzRule;
  Schedule schedule = Schedule();
  uint32_t freqHz = kFreqDefaultHz;
};

DeviceSettings settingsDefaults();
bool settingsValidWifi(const std::string &ssid, const std::string &pass, std::string &error);
bool settingsValidTzRule(const std::string &rule);
bool settingsResolveTz(const std::string &id, const std::string &customRule, std::string &ruleOut);
bool settingsValidFreq(uint32_t hz);
bool parseHHMM(const std::string &text, uint16_t &minutesOut);
std::string formatHHMM(uint16_t minutes);
```

- [ ] **Step 4: Create `lib/jjy_core/src/settings_rules.cpp`**

```cpp
#include "settings_rules.h"

#include <ctype.h>
#include <stdio.h>

#include "timezones.h"

DeviceSettings settingsDefaults() {
  static const uint16_t kDefaultSlots[] = {76, 256, 796, 976};
  DeviceSettings s;
  s.tzId = "warsaw";
  s.tzRule = tzFindById("warsaw")->posix;
  for (uint8_t i = 0; i < 4; ++i) s.schedule.slotMinutes[i] = kDefaultSlots[i];
  s.schedule.count = 4;
  s.schedule.windowMinutes = 5;
  s.freqHz = kFreqDefaultHz;
  return s;
}

bool settingsValidWifi(const std::string &ssid, const std::string &pass, std::string &error) {
  if (ssid.empty()) {
    error = "Please choose a WiFi network.";
    return false;
  }
  if (ssid.size() > 32) {
    error = "WiFi network names are at most 32 characters.";
    return false;
  }
  if (!pass.empty() && (pass.size() < 8 || pass.size() > 63)) {
    error = "WiFi passwords are 8 to 63 characters long.";
    return false;
  }
  return true;
}

bool settingsValidTzRule(const std::string &rule) {
  if (rule.size() < 3 || rule.size() > 64) return false;
  if (!isalpha(static_cast<unsigned char>(rule[0])) && rule[0] != '<') return false;
  for (char c : rule) {
    if (c < 0x21 || c > 0x7e) return false;
  }
  return true;
}

bool settingsResolveTz(const std::string &id, const std::string &customRule, std::string &ruleOut) {
  if (id == "custom") {
    if (!settingsValidTzRule(customRule)) return false;
    ruleOut = customRule;
    return true;
  }
  const TzEntry *entry = tzFindById(id.c_str());
  if (entry == nullptr) return false;
  ruleOut = entry->posix;
  return true;
}

bool settingsValidFreq(uint32_t hz) { return hz >= kFreqMinHz && hz <= kFreqMaxHz; }

bool parseHHMM(const std::string &text, uint16_t &minutesOut) {
  size_t i = 0;
  int hour = 0;
  int digits = 0;
  while (i < text.size() && isdigit(static_cast<unsigned char>(text[i])) && digits < 2) {
    hour = hour * 10 + (text[i] - '0');
    ++i;
    ++digits;
  }
  if (digits == 0 || i >= text.size() || text[i] != ':') return false;
  ++i;
  int minute = 0;
  digits = 0;
  while (i < text.size() && isdigit(static_cast<unsigned char>(text[i])) && digits < 2) {
    minute = minute * 10 + (text[i] - '0');
    ++i;
    ++digits;
  }
  if (digits != 2) return false;
  if (i < text.size() && text[i] != ':') return false;
  if (hour > 23 || minute > 59) return false;
  minutesOut = static_cast<uint16_t>(hour * 60 + minute);
  return true;
}

std::string formatHHMM(uint16_t minutes) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%02u:%02u", static_cast<unsigned>(minutes / 60), static_cast<unsigned>(minutes % 60));
  return buf;
}
```

- [ ] **Step 5: Create `lib/jjy_core/src/version_compare.h`**

```cpp
#pragma once

// Compares "1.2.3" style versions (optional leading 'v', suffix after a non-digit ignored).
// Unparsable versions such as "dev" are older than every release. Returns -1, 0 or 1.
int versionCompare(const char *a, const char *b);
```

- [ ] **Step 6: Create `lib/jjy_core/src/version_compare.cpp`**

```cpp
#include "version_compare.h"

#include <ctype.h>

namespace {

bool parseVersion(const char *text, int parts[3]) {
  parts[0] = parts[1] = parts[2] = 0;
  if (text == nullptr) return false;
  if (*text == 'v' || *text == 'V') ++text;
  if (!isdigit(static_cast<unsigned char>(*text))) return false;
  for (int i = 0; i < 3; ++i) {
    if (!isdigit(static_cast<unsigned char>(*text))) break;
    int value = 0;
    while (isdigit(static_cast<unsigned char>(*text))) {
      value = value * 10 + (*text - '0');
      ++text;
    }
    parts[i] = value;
    if (*text != '.') break;
    ++text;
  }
  return true;
}

}  // namespace

int versionCompare(const char *a, const char *b) {
  int pa[3];
  int pb[3];
  const bool validA = parseVersion(a, pa);
  const bool validB = parseVersion(b, pb);
  if (!validA || !validB) return (validA ? 1 : 0) - (validB ? 1 : 0);
  for (int i = 0; i < 3; ++i) {
    if (pa[i] != pb[i]) return pa[i] < pb[i] ? -1 : 1;
  }
  return 0;
}
```

- [ ] **Step 7: Run all PC tests**

Run: `~/.venvs/pio/bin/pio test -e native`
Expected: every suite reports `PASSED`, and `test_settings_rules` reports `7 Tests 0 Failures`.

- [ ] **Step 8: Commit**

```bash
git add lib/jjy_core/src/settings_rules.* lib/jjy_core/src/version_compare.* test/test_settings_rules
git commit -m "Add settings validation, HH:MM parsing and version comparison"
```

---

### Task 5: HTML rendering and escaping

**Files:**
- Create: `lib/jjy_core/src/html.h`, `lib/jjy_core/src/html.cpp`
- Test: `test/test_html/test_main.cpp`

**Interfaces:**
- Consumes: `kTimezones`/`kTimezoneCount` (Task 2), `Schedule` (Task 3), `formatHHMM` (Task 4).
- Produces:
  - `struct SetupPageView { std::vector<std::string> networks; std::string selectedSsid, tzId, customTzRule; Schedule schedule; bool autoDetectTz; std::string error; };`
  - `struct SettingsPageView { std::string localTime, nextBroadcast; bool broadcasting; std::string tzId, customTzRule; Schedule schedule; uint32_t freqHz; std::string version, updateVersion, notice, error; };`
  - `std::string htmlEscape(const std::string &)`, `std::string jsonEscape(const std::string &)`
  - `std::string renderSetupPage(const SetupPageView &)`, `std::string renderConnectingPage()`, `std::string renderSettingsPage(const SettingsPageView &)`, `std::string renderUpdatingPage(const std::string &currentVersion)`, `std::string renderMessagePage(const std::string &title, const std::string &text)`
- Form field names that later tasks rely on:
  - setup form: `ssid`, `ssid_other`, `pass`, `tz`, `tz_custom`, `slot` (repeated) and `win`, posted to `/setup`;
  - settings form: `tz`, `tz_custom`, `slot`, `win` and `freq`, posted to `/save`;
  - update forms post to `/update/check` and `/update/install`;
  - "Forget WiFi" posts to `/forget`;
  - the connecting page polls `/setup/status`, which returns `{state, ip}`;
  - the updating page polls `/status`, which returns `{version, ota, error}`.

- [ ] **Step 1: Write the failing test `test/test_html/test_main.cpp`**

```cpp
#include <unity.h>

#include <string>

#include "html.h"

void setUp() {}
void tearDown() {}

static bool contains(const std::string &haystack, const std::string &needle) {
  return haystack.find(needle) != std::string::npos;
}

static Schedule twoSlots() {
  Schedule s = Schedule();
  s.slotMinutes[0] = 76;
  s.slotMinutes[1] = 976;
  s.count = 2;
  s.windowMinutes = 5;
  return s;
}

void test_html_escape() {
  TEST_ASSERT_EQUAL_STRING("&lt;a href=&quot;x&quot;&gt;&#39;&amp;&#39;&lt;/a&gt;",
                           htmlEscape("<a href=\"x\">'&'</a>").c_str());
}

void test_json_escape() {
  TEST_ASSERT_EQUAL_STRING("a\\\"b\\\\c\\n\\u003c/script\\u003e", jsonEscape("a\"b\\c\n</script>").c_str());
}

void test_setup_page() {
  SetupPageView v;
  v.networks.push_back("Home");
  v.networks.push_back("<evil>");
  v.selectedSsid = "Home";
  v.tzId = "warsaw";
  v.schedule = twoSlots();
  v.autoDetectTz = true;
  v.error = "Bad & wrong";
  const std::string page = renderSetupPage(v);
  TEST_ASSERT_TRUE(contains(page, "<option value=\"Home\" selected>Home</option>"));
  TEST_ASSERT_TRUE(contains(page, "&lt;evil&gt;"));
  TEST_ASSERT_FALSE(contains(page, "<evil>"));
  TEST_ASSERT_TRUE(contains(page, "Bad &amp; wrong"));
  TEST_ASSERT_TRUE(contains(page, "name=\"slot\" required value=\"01:16\""));
  TEST_ASSERT_TRUE(contains(page, "name=\"slot\" required value=\"16:16\""));
  TEST_ASSERT_TRUE(contains(page, "value=\"warsaw\" data-iana=\"Europe/Warsaw\" selected"));
  TEST_ASSERT_TRUE(contains(page, "resolvedOptions().timeZone"));
  TEST_ASSERT_TRUE(contains(page, "action=\"/setup\""));
}

void test_setup_page_other_network_when_unmatched() {
  SetupPageView v;
  v.selectedSsid = "Hidden";
  v.tzId = "warsaw";
  v.schedule = twoSlots();
  const std::string page = renderSetupPage(v);
  TEST_ASSERT_TRUE(contains(page, "<option value=\"\" selected>"));
  TEST_ASSERT_TRUE(contains(page, "name=\"ssid_other\" autocapitalize=\"none\" autocorrect=\"off\" value=\"Hidden\""));
}

void test_settings_page() {
  SettingsPageView v;
  v.localTime = "13:18";
  v.nextBroadcast = "16:16";
  v.tzId = "tokyo";
  v.schedule = twoSlots();
  v.freqHz = 40010;
  v.version = "1.2.0";
  const std::string page = renderSettingsPage(v);
  TEST_ASSERT_TRUE(contains(page, "value=\"tokyo\" data-iana=\"Asia/Tokyo\" selected"));
  TEST_ASSERT_TRUE(contains(page, "Next broadcast at 16:16"));
  TEST_ASSERT_TRUE(contains(page, "value=\"40010\""));
  TEST_ASSERT_TRUE(contains(page, "Software version 1.2.0"));
  TEST_ASSERT_TRUE(contains(page, "action=\"/update/check\""));
  TEST_ASSERT_FALSE(contains(page, "action=\"/update/install\""));
  TEST_ASSERT_TRUE(contains(page, "action=\"/forget\""));
  TEST_ASSERT_FALSE(contains(page, "resolvedOptions().timeZone"));
}

void test_settings_page_update_and_broadcasting() {
  SettingsPageView v;
  v.localTime = "13:18";
  v.broadcasting = true;
  v.tzId = "warsaw";
  v.schedule = twoSlots();
  v.version = "1.2.0";
  v.updateVersion = "1.3.0";
  const std::string page = renderSettingsPage(v);
  TEST_ASSERT_TRUE(contains(page, "Broadcasting now"));
  TEST_ASSERT_TRUE(contains(page, "Install version 1.3.0"));
  TEST_ASSERT_TRUE(contains(page, "action=\"/update/install\""));
}

void test_waiting_for_time() {
  SettingsPageView v;
  v.tzId = "warsaw";
  v.schedule = twoSlots();
  TEST_ASSERT_TRUE(contains(renderSettingsPage(v), "Waiting for the time from the internet"));
}

void test_no_external_resources() {
  SetupPageView setup;
  setup.tzId = "warsaw";
  setup.schedule = twoSlots();
  SettingsPageView settings;
  settings.tzId = "warsaw";
  settings.schedule = twoSlots();
  const std::string pages[] = {renderSetupPage(setup), renderSettingsPage(settings), renderConnectingPage(),
                               renderUpdatingPage("1.0.0"), renderMessagePage("t", "x")};
  for (const std::string &p : pages) {
    TEST_ASSERT_FALSE(contains(p, "<script src"));
    TEST_ASSERT_FALSE(contains(p, "<link"));
    TEST_ASSERT_TRUE(contains(p, "<style>"));
  }
}

void test_updating_page_embeds_version_safely() {
  TEST_ASSERT_TRUE(contains(renderUpdatingPage("1.0.0"), "var cur=\"1.0.0\""));
  TEST_ASSERT_TRUE(contains(renderUpdatingPage("</script>"), "\\u003c/script\\u003e"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_html_escape);
  RUN_TEST(test_json_escape);
  RUN_TEST(test_setup_page);
  RUN_TEST(test_setup_page_other_network_when_unmatched);
  RUN_TEST(test_settings_page);
  RUN_TEST(test_settings_page_update_and_broadcasting);
  RUN_TEST(test_waiting_for_time);
  RUN_TEST(test_no_external_resources);
  RUN_TEST(test_updating_page_embeds_version_safely);
  return UNITY_END();
}
```

- [ ] **Step 2: Run it to make sure it fails**

Run: `~/.venvs/pio/bin/pio test -e native -f test_html`
Expected: FAIL with `html.h: No such file or directory`.

- [ ] **Step 3: Create `lib/jjy_core/src/html.h`**

```cpp
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include "schedule.h"

struct SetupPageView {
  std::vector<std::string> networks;
  std::string selectedSsid;
  std::string tzId;
  std::string customTzRule;
  Schedule schedule = Schedule();
  bool autoDetectTz = false;
  std::string error;
};

struct SettingsPageView {
  std::string localTime;      // empty while the clock is not valid
  std::string nextBroadcast;  // "HH:MM", empty if unknown
  bool broadcasting = false;
  std::string tzId;
  std::string customTzRule;
  Schedule schedule = Schedule();
  uint32_t freqHz = 40000;
  std::string version;
  std::string updateVersion;  // non-empty shows the Install button
  std::string notice;
  std::string error;
};

std::string htmlEscape(const std::string &text);
std::string jsonEscape(const std::string &text);
std::string renderSetupPage(const SetupPageView &view);
std::string renderConnectingPage();
std::string renderSettingsPage(const SettingsPageView &view);
std::string renderUpdatingPage(const std::string &currentVersion);
std::string renderMessagePage(const std::string &title, const std::string &text);
```

- [ ] **Step 4: Create `lib/jjy_core/src/html.cpp`**

```cpp
#include "html.h"

#include <stdio.h>

#include "settings_rules.h"
#include "timezones.h"

namespace {

const char kCss[] =
    ":root{color-scheme:light dark;--bg:#f6f7f9;--card:#fff;--text:#1b1f24;--muted:#5b6470;--line:#d9dde3;"
    "--accent:#2563eb;--on-accent:#fff;--danger:#b42318;--ok:#067647}"
    "@media(prefers-color-scheme:dark){:root{--bg:#111418;--card:#1b2027;--text:#e8eaed;--muted:#9aa3ad;"
    "--line:#2e3540;--accent:#5b8def;--on-accent:#0b0d10;--danger:#f97066;--ok:#47cd89}}"
    "*{box-sizing:border-box}"
    "body{margin:0;background:var(--bg);color:var(--text);"
    "font:16px/1.5 system-ui,-apple-system,'Segoe UI',Roboto,sans-serif}"
    "main{max-width:480px;margin:0 auto;padding:16px}"
    "h1{font-size:22px;margin:8px 0 16px}h2{font-size:17px;margin:0 0 4px}"
    ".card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px;margin:0 0 16px}"
    "label{display:block;font-weight:600;margin:12px 0 4px}"
    "input,select,button{font:inherit;width:100%;padding:12px;border:1px solid var(--line);border-radius:8px;"
    "background:var(--card);color:var(--text)}"
    "button{background:var(--accent);color:var(--on-accent);border:0;font-weight:600;margin-top:12px;cursor:pointer}"
    "button.secondary{background:transparent;color:var(--accent);border:1px solid var(--line)}"
    "button.danger{background:transparent;color:var(--danger);border:1px solid var(--line)}"
    ".row{display:flex;gap:8px;margin-bottom:8px}.row input{flex:1}"
    ".row button{width:auto;margin:0;padding:12px 16px}"
    ".help{color:var(--muted);font-size:14px;margin:6px 0 0}"
    ".msg{padding:12px;border-radius:8px;margin:0 0 16px;border:1px solid var(--line);background:var(--card)}"
    ".msg.error{color:var(--danger)}.msg.ok{color:var(--ok)}"
    ".big{font-size:32px;font-weight:700;line-height:1.2}"
    "summary{cursor:pointer;font-weight:600}a{color:var(--accent)}";

const char kScheduleScript[] =
    "var slots=document.getElementById('slots');"
    "function slotRm(b){if(slots.children.length>1)slots.removeChild(b.parentNode)}"
    "document.getElementById('slotAdd').onclick=function(){if(slots.children.length>=8)return;"
    "var r=slots.children[0].cloneNode(true);r.querySelector('input').value='';slots.appendChild(r)};";

std::string page(const std::string &title, const std::string &body, const std::string &script) {
  std::string out;
  out.reserve(body.size() + script.size() + sizeof(kCss) + 300);
  out += "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
         "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>";
  out += htmlEscape(title);
  out += "</title><style>";
  out += kCss;
  out += "</style></head><body><main>";
  out += body;
  out += "</main>";
  if (!script.empty()) {
    out += "<script>";
    out += script;
    out += "</script>";
  }
  out += "</body></html>";
  return out;
}

std::string message(const std::string &text, bool isError) {
  if (text.empty()) return "";
  std::string out = "<div class=\"msg ";
  out += isError ? "error" : "ok";
  out += "\" role=\"status\">";
  out += htmlEscape(text);
  out += "</div>";
  return out;
}

std::string timezoneFields(const std::string &tzId, const std::string &customRule) {
  std::string out = "<label for=\"tz\">Time zone</label><select id=\"tz\" name=\"tz\">";
  for (size_t i = 0; i < kTimezoneCount; ++i) {
    const TzEntry &e = kTimezones[i];
    out += "<option value=\"";
    out += htmlEscape(e.id);
    out += "\" data-iana=\"";
    out += htmlEscape(e.iana);
    out += "\"";
    if (tzId == e.id) out += " selected";
    out += ">";
    out += htmlEscape(e.label);
    out += "</option>";
  }
  out += "<option value=\"custom\"";
  if (tzId == "custom") out += " selected";
  out += ">Custom rule (advanced)</option></select>";
  out += "<p class=\"help\">Your clock will show the time of the city you pick.</p>";
  out += "<div id=\"tzc\" hidden><label for=\"tz_custom\">Custom POSIX rule</label>"
         "<input id=\"tz_custom\" name=\"tz_custom\" autocapitalize=\"none\" value=\"";
  out += htmlEscape(customRule);
  out += "\"></div>";
  return out;
}

std::string timezoneScript(bool autoDetect) {
  std::string s =
      "var tz=document.getElementById('tz'),tzc=document.getElementById('tzc');"
      "function tzUpd(){tzc.hidden=tz.value!=='custom'}tz.onchange=tzUpd;";
  if (autoDetect) {
    s += "try{var z=' '+Intl.DateTimeFormat().resolvedOptions().timeZone+' ';"
         "for(var i=0;i<tz.options.length;i++){var a=tz.options[i].getAttribute('data-iana');"
         "if(a&&(' '+a+' ').indexOf(z)>=0){tz.selectedIndex=i;break}}}catch(e){}";
  }
  s += "tzUpd();";
  return s;
}

std::string scheduleFields(const Schedule &schedule) {
  std::string out = "<label>Start times</label><div id=\"slots\">";
  const uint8_t rows = schedule.count > 0 ? schedule.count : 1;
  for (uint8_t i = 0; i < rows && i < kMaxSlots; ++i) {
    out += "<div class=\"row\"><input type=\"time\" name=\"slot\" required value=\"";
    if (i < schedule.count) out += formatHHMM(schedule.slotMinutes[i]);
    out += "\"><button type=\"button\" class=\"secondary\" aria-label=\"Remove this time\" "
           "onclick=\"slotRm(this)\">&#10005;</button></div>";
  }
  out += "</div><button type=\"button\" class=\"secondary\" id=\"slotAdd\">Add a time</button>";
  char window[8];
  snprintf(window, sizeof(window), "%u", static_cast<unsigned>(schedule.windowMinutes));
  out += "<label for=\"win\">Broadcast length (minutes)</label>"
         "<input id=\"win\" name=\"win\" type=\"number\" inputmode=\"numeric\" min=\"1\" max=\"30\" required value=\"";
  out += window;
  out += "\"><p class=\"help\">Radio clocks usually check the time at night. "
         "If you don't know when yours does, keep these times.</p>";
  return out;
}

}  // namespace

std::string htmlEscape(const std::string &text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      case '\'':
        out += "&#39;";
        break;
      default:
        out += c;
    }
  }
  return out;
}

std::string jsonEscape(const std::string &text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    const unsigned char u = static_cast<unsigned char>(c);
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      case '<':
        out += "\\u003c";
        break;
      case '>':
        out += "\\u003e";
        break;
      case '&':
        out += "\\u0026";
        break;
      default:
        if (u < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", u);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

std::string renderSetupPage(const SetupPageView &v) {
  std::string b = "<h1>Set up your JJY clock transmitter</h1>";
  b += message(v.error, true);
  b += "<form method=\"post\" action=\"/setup\">";
  b += "<section class=\"card\"><h2>1. WiFi</h2><label for=\"ssid\">Network</label><select id=\"ssid\" name=\"ssid\">";
  bool matched = false;
  for (const std::string &network : v.networks) {
    const bool selected = !matched && network == v.selectedSsid;
    matched = matched || selected;
    b += "<option value=\"";
    b += htmlEscape(network);
    b += "\"";
    if (selected) b += " selected";
    b += ">";
    b += htmlEscape(network);
    b += "</option>";
  }
  b += "<option value=\"\"";
  if (!matched) b += " selected";
  b += ">Other network&hellip;</option></select>";
  b += "<div id=\"other\" hidden><label for=\"ssid_other\">Network name</label>"
       "<input id=\"ssid_other\" name=\"ssid_other\" autocapitalize=\"none\" autocorrect=\"off\" value=\"";
  if (!matched) b += htmlEscape(v.selectedSsid);
  b += "\"></div>";
  b += "<label for=\"pass\">Password</label><input id=\"pass\" name=\"pass\" type=\"password\">";
  b += "<p class=\"help\"><a href=\"/?rescan=1\">Don't see your network? Refresh the list</a></p></section>";
  b += "<section class=\"card\"><h2>2. Where are you?</h2>";
  b += timezoneFields(v.tzId, v.customTzRule);
  b += "</section><section class=\"card\"><h2>3. When does your clock check the time?</h2>";
  b += scheduleFields(v.schedule);
  b += "</section><button type=\"submit\">Connect</button></form>";

  std::string script =
      "var ss=document.getElementById('ssid'),so=document.getElementById('other');"
      "function ssUpd(){so.hidden=ss.value!==''}ss.onchange=ssUpd;ssUpd();";
  script += timezoneScript(v.autoDetectTz);
  script += kScheduleScript;
  return page("JJY setup", b, script);
}

std::string renderConnectingPage() {
  const std::string body =
      "<div id=\"st\" class=\"card\"><h1>Connecting&hellip;</h1>"
      "<p>This takes up to 20 seconds. Keep this page open.</p>"
      "<p class=\"help\">If this page closes, join the JJY-Setup network again to see the result.</p></div>";
  const std::string script =
      "function poll(){fetch('/setup/status').then(function(r){return r.json()}).then(function(s){"
      "if(s.state==='connected'){var st=document.getElementById('st');"
      "st.innerHTML='<h1>Done!</h1><p>The transmitter is connected to your WiFi.</p>"
      "<p>To change settings later, connect your phone to your home WiFi and open "
      "<a href=\"http://jjy.local\">http://jjy.local</a></p>"
      "<p class=\"help\">If that address does not open, use <span id=\"ip\"></span></p>"
      "<p>This setup network turns off in a moment.</p>';"
      "document.getElementById('ip').textContent='http://'+s.ip;return}"
      "if(s.state==='failed'){location.href='/?failed=1';return}"
      "setTimeout(poll,2000)}).catch(function(){setTimeout(poll,2000)})}"
      "setTimeout(poll,2000);";
  return page("Connecting", body, script);
}

std::string renderSettingsPage(const SettingsPageView &v) {
  std::string b = "<h1>JJY clock transmitter</h1>";
  b += message(v.error, true);
  b += message(v.notice, false);
  b += "<section class=\"card\">";
  if (v.localTime.empty()) {
    b += "<p>Waiting for the time from the internet&hellip;</p>";
  } else {
    b += "<div class=\"big\">";
    b += htmlEscape(v.localTime);
    b += "</div>";
    if (v.broadcasting) {
      b += "<p>Broadcasting now</p>";
    } else if (!v.nextBroadcast.empty()) {
      b += "<p>Next broadcast at ";
      b += htmlEscape(v.nextBroadcast);
      b += "</p>";
    }
  }
  b += "</section>";

  b += "<form method=\"post\" action=\"/save\">";
  b += "<section class=\"card\"><h2>Time zone</h2>";
  b += timezoneFields(v.tzId, v.customTzRule);
  b += "</section><section class=\"card\"><h2>When does your clock check the time?</h2>";
  b += scheduleFields(v.schedule);
  b += "</section>";
  char freq[16];
  snprintf(freq, sizeof(freq), "%lu", static_cast<unsigned long>(v.freqHz));
  b += "<details class=\"card\"><summary>Advanced</summary>"
       "<label for=\"freq\">Carrier frequency (Hz)</label>"
       "<input id=\"freq\" name=\"freq\" type=\"number\" min=\"39500\" max=\"40500\" step=\"10\" value=\"";
  b += freq;
  b += "\"><p class=\"help\">Only change this if you are tuning the antenna.</p></details>";
  b += "<button type=\"submit\">Save</button></form>";

  b += "<section class=\"card\"><h2>Device</h2><p>Software version ";
  b += htmlEscape(v.version);
  b += "</p>";
  if (!v.updateVersion.empty()) {
    b += "<form method=\"post\" action=\"/update/install\"><button type=\"submit\">Install version ";
    b += htmlEscape(v.updateVersion);
    b += "</button></form>";
  } else {
    b += "<form method=\"post\" action=\"/update/check\">"
         "<button type=\"submit\" class=\"secondary\">Check for updates</button></form>";
  }
  b += "<form method=\"post\" action=\"/forget\" onsubmit=\"return confirm("
       "'Forget this WiFi network? You will need to set the transmitter up again.')\">"
       "<button type=\"submit\" class=\"danger\">Forget WiFi</button></form></section>";

  std::string script = timezoneScript(false);
  script += kScheduleScript;
  return page("JJY clock transmitter", b, script);
}

std::string renderUpdatingPage(const std::string &currentVersion) {
  const std::string body =
      "<div id=\"st\" class=\"card\"><h1>Updating&hellip;</h1>"
      "<p>Don't unplug the transmitter. This takes about a minute.</p></div>";
  std::string script = "var cur=\"";
  script += jsonEscape(currentVersion);
  script +=
      "\",t0=Date.now();"
      "function show(h,t){var st=document.getElementById('st');st.innerHTML=h;"
      "if(t){var p=document.createElement('p');p.textContent=t;st.appendChild(p)}}"
      "function next(){if(Date.now()-t0>180000){show('<h1>This is taking too long</h1>',"
      "'Unplug the transmitter, plug it back in, then open this page again.');return}setTimeout(poll,3000)}"
      "function poll(){fetch('/status').then(function(r){return r.json()}).then(function(s){"
      "if(s.version!==cur){show('<h1>Updated</h1><p><a href=\"/\">Back to settings</a></p>',"
      "'Now running version '+s.version+'.');return}"
      "if(s.ota==='failed'){show('<h1>Update failed</h1><p><a href=\"/\">Back to settings</a></p>',s.error);return}"
      "next()}).catch(next)}"
      "setTimeout(poll,5000);";
  return page("Updating", body, script);
}

std::string renderMessagePage(const std::string &title, const std::string &text) {
  std::string b = "<div class=\"card\"><h1>";
  b += htmlEscape(title);
  b += "</h1><p>";
  b += htmlEscape(text);
  b += "</p></div>";
  return page(title, b, "");
}
```

- [ ] **Step 5: Run all PC tests**

Run: `~/.venvs/pio/bin/pio test -e native`
Expected: all five suites `PASSED`. `test_html` reports `9 Tests 0 Failures`.

- [ ] **Step 6: Look at the rendered pages in a browser**

Generate the pages with a throwaway program, open them in a desktop browser at phone width (about 375 px), and check both light and dark mode:

```bash
D=$(mktemp -d) && cat > "$D/render_pages.cpp" <<'EOF'
#include <fstream>
#include "html.h"
int main() {
  Schedule s = Schedule(); s.slotMinutes[0] = 76; s.slotMinutes[1] = 976; s.count = 2; s.windowMinutes = 5;
  SetupPageView a; a.networks = {"Home", "Neighbour"}; a.tzId = "warsaw"; a.schedule = s; a.autoDetectTz = true;
  SettingsPageView b; b.localTime = "13:18"; b.nextBroadcast = "16:16"; b.tzId = "warsaw"; b.schedule = s; b.version = "1.0.0";
  std::ofstream("setup.html") << renderSetupPage(a);
  std::ofstream("settings.html") << renderSettingsPage(b);
}
EOF
g++ -std=gnu++11 -Ilib/jjy_core/src lib/jjy_core/src/*.cpp "$D/render_pages.cpp" -o "$D/render_pages" && (cd "$D" && ./render_pages) && echo "$D"
```

Open `setup.html` and `settings.html` from the printed directory.

Check that:
- the forms fit at phone width without horizontal scrolling;
- "Add a time" and the remove buttons work;
- choosing "Custom rule (advanced)" shows the rule field;
- on the setup page, "Other network…" shows the name field.

- [ ] **Step 7: Commit**

```bash
git add lib/jjy_core/src/html.h lib/jjy_core/src/html.cpp test/test_html
git commit -m "Add escaped page rendering for setup and settings pages"
```

---

### Task 6: Rewrite the firmware core (carrier, transmitter, time sync, settings storage, schedule controller)

This replaces `src/main.cpp`. The old web UI, override mode, deep-sleep spam and `secrets.h` go away. After this task the firmware runs from WiFi credentials stored in NVS. With nothing stored yet, it logs `No WiFi saved` and waits; Task 7 adds the setup hotspot.

**Files:**
- Create: `src/version.h`, `src/app_state.h`, `src/carrier.h`, `src/carrier.cpp`, `src/transmitter.h`, `src/transmitter.cpp`, `src/time_sync.h`, `src/time_sync.cpp`, `src/settings_store.h`, `src/settings_store.cpp`
- Rewrite: `src/main.cpp`
- Delete: `include/secrets.h.example`, `include/secrets.h` (the local, git-ignored file that holds the WiFi password)
- Modify: `.gitignore` (remove the line `include/secrets.h`)

**Interfaces:**
- Consumes: `jjyBuildFrame`/`jjyOnDurationMs`/`jjySymbolChar` (Task 1), the schedule functions (Task 3), and `DeviceSettings`/`settingsDefaults`/`settingsValidFreq`/`settingsResolveTz`/`kFreqMinHz`/`kFreqMaxHz` (Task 4).
- Produces:
  - `version.h`: `FW_VERSION`, which is a string literal.
  - `app_state.h`: `enum class OtaState : uint8_t { IDLE, INSTALL_REQUESTED, INSTALLING, FAILED };` and `struct AppState { DeviceSettings settings; bool coldBootBroadcast; bool uiHold; uint32_t restartAtMs; OtaState otaState; std::string otaError, latestVersion, latestUrl; };` with `extern AppState app;`, defined in `main.cpp`.
  - `carrier.h`: `kCarrierRampMs = 5`, `carrierBegin(uint32_t)`, `carrierSetFrequency(uint32_t)`, `carrierFrequency()`, `carrierRampUp()`, `carrierRampDown()`, `carrierOff()`, `carrierTick()`, `carrierIsOn()`.
  - `transmitter.h`: `txBegin()`, `txStartWindow(time_t end)`, `txStopWindow()`, `txWindowActive()`, `txWindowEnd()`.
  - `time_sync.h`: `timeSyncSetTz(const char*)`, `timeSyncBegin(const char*)`, `timeSyncWaitForNtp(uint32_t ms)`, `timeSyncClockValid()`.
  - `settings_store.h`: `settingsLoad(DeviceSettings&)`, `settingsSave(const DeviceSettings&)`, `settingsForgetWifi()`, `settingsIncrementBootCount()` returning `uint8_t`, `settingsClearBootCount()`.

- [ ] **Step 1: Create `src/version.h`**

```cpp
#pragma once

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif
```

- [ ] **Step 2: Create `src/app_state.h`**

```cpp
#pragma once

#include <stdint.h>

#include <string>

#include "settings_rules.h"

enum class OtaState : uint8_t { IDLE, INSTALL_REQUESTED, INSTALLING, FAILED };

// Touched only from the Arduino loop task (setup, loop and web handlers).
struct AppState {
  DeviceSettings settings;
  bool coldBootBroadcast = false;
  bool uiHold = false;
  uint32_t restartAtMs = 0;  // 0 means no restart pending
  OtaState otaState = OtaState::IDLE;
  std::string otaError;
  std::string latestVersion;  // newer release found by the last check
  std::string latestUrl;
};

extern AppState app;
```

- [ ] **Step 3: Create `src/carrier.h`**

```cpp
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
```

- [ ] **Step 4: Create `src/carrier.cpp`** (MCPWM setup and duty logic ported from the old `setupCarrier`/`applyCarrierDuty`; the ramp now steps in microseconds, so it really lasts 5 ms)

```cpp
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
  mcpwm_deadtime_enable(kUnit, kTimer, MCPWM_ACTIVE_HIGH_MODE, dt, dt);
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
```

- [ ] **Step 5: Create `src/time_sync.h` and `src/time_sync.cpp`**

```cpp
#pragma once

#include <stdint.h>

void timeSyncSetTz(const char *tzRule);
void timeSyncBegin(const char *tzRule);
bool timeSyncWaitForNtp(uint32_t timeoutMs);
bool timeSyncClockValid();
```

```cpp
#include "time_sync.h"

#include <Arduino.h>
#include <esp_sntp.h>
#include <time.h>

namespace {
const char *kNtp1 = "pool.ntp.org";
const char *kNtp2 = "time.google.com";
const time_t kMinValidEpoch = 1700000000;  // 2023-11-14
}  // namespace

void timeSyncSetTz(const char *tzRule) {
  setenv("TZ", tzRule, 1);
  tzset();
}

void timeSyncBegin(const char *tzRule) {
  sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
  configTzTime(tzRule, kNtp1, kNtp2);
}

bool timeSyncWaitForNtp(uint32_t timeoutMs) {
  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) return true;
    delay(100);
  }
  return false;
}

bool timeSyncClockValid() { return time(nullptr) >= kMinValidEpoch; }
```

- [ ] **Step 6: Create `src/transmitter.h` and `src/transmitter.cpp`** (the old `runJjySchedulerTick`. The window state sits behind a spinlock and a generation counter, which fixes the start-of-window race.)

```cpp
#pragma once

#include <time.h>

void txBegin();
void txStartWindow(time_t end);
void txStopWindow();
bool txWindowActive();
time_t txWindowEnd();
```

```cpp
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
const uint32_t kStackWords = 4096;

portMUX_TYPE windowMux = portMUX_INITIALIZER_UNLOCKED;
bool windowActive = false;
time_t windowEnd = 0;
uint32_t windowGeneration = 0;

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
      frameMinute = -1;
      pulseActive = false;
      carrierOff();
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

void txBegin() { xTaskCreatePinnedToCore(txTask, "jjy_tx", kStackWords, nullptr, kPriority, nullptr, kCore); }

void txStartWindow(time_t end) {
  portENTER_CRITICAL(&windowMux);
  windowActive = true;
  windowEnd = end;
  ++windowGeneration;
  portEXIT_CRITICAL(&windowMux);
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
```

- [ ] **Step 7: Create `src/settings_store.h` and `src/settings_store.cpp`**

```cpp
#pragma once

#include <stdint.h>

#include "settings_rules.h"

void settingsLoad(DeviceSettings &settings);
void settingsSave(const DeviceSettings &settings);
void settingsForgetWifi();
uint8_t settingsIncrementBootCount();
void settingsClearBootCount();
```

```cpp
#include "settings_store.h"

#include <Arduino.h>
#include <Preferences.h>

#include "timezones.h"

namespace {

Preferences prefs;
bool ready = false;

bool open() {
  if (!ready) ready = prefs.begin("jjy", false);
  return ready;
}

std::string readString(const char *key) {
  if (!prefs.isKey(key)) return std::string();
  return std::string(prefs.getString(key, "").c_str());
}

}  // namespace

void settingsLoad(DeviceSettings &s) {
  s = settingsDefaults();
  if (!open()) {
    Serial.println("[NVS] Could not open settings; using defaults.");
    return;
  }

  s.wifiSsid = readString("wifi_ssid");
  s.wifiPass = readString("wifi_pass");

  std::string tzId = readString("tz_id");
  std::string tzRule = readString("tz_rule");
  if (tzId.empty() && !tzRule.empty()) {
    tzId = tzRule == tzFindById("warsaw")->posix ? "warsaw" : "custom";
    Serial.printf("[NVS] Migrated time zone rule to id '%s'\n", tzId.c_str());
  }
  std::string resolved;
  if (!tzId.empty() && settingsResolveTz(tzId, tzRule, resolved)) {
    s.tzId = tzId;
    s.tzRule = resolved;
  }

  if (prefs.isKey("web_ovrd")) prefs.remove("web_ovrd");

  const size_t length = prefs.isKey("slots") ? prefs.getBytesLength("slots") : 0;
  if (length >= sizeof(uint16_t) && length <= sizeof(s.schedule.slotMinutes) && length % sizeof(uint16_t) == 0) {
    Schedule loaded = Schedule();
    prefs.getBytes("slots", loaded.slotMinutes, length);
    loaded.count = static_cast<uint8_t>(length / sizeof(uint16_t));
    loaded.windowMinutes = prefs.getUChar("win_min", 5);
    if (scheduleValidate(loaded) == ScheduleError::OK) {
      scheduleSort(loaded);
      s.schedule = loaded;
    }
  }

  const uint32_t hz = prefs.isKey("freq_hz") ? prefs.getUInt("freq_hz", kFreqDefaultHz) : kFreqDefaultHz;
  if (settingsValidFreq(hz)) s.freqHz = hz;

  Serial.printf("[NVS] WiFi '%s', time zone %s, %u start times, %lu Hz\n", s.wifiSsid.c_str(), s.tzId.c_str(),
                static_cast<unsigned>(s.schedule.count), static_cast<unsigned long>(s.freqHz));
}

void settingsSave(const DeviceSettings &s) {
  if (!open()) return;
  prefs.putString("wifi_ssid", s.wifiSsid.c_str());
  prefs.putString("wifi_pass", s.wifiPass.c_str());
  prefs.putString("tz_id", s.tzId.c_str());
  prefs.putString("tz_rule", s.tzRule.c_str());
  prefs.putBytes("slots", s.schedule.slotMinutes, s.schedule.count * sizeof(uint16_t));
  prefs.putUChar("win_min", s.schedule.windowMinutes);
  prefs.putUInt("freq_hz", s.freqHz);
}

void settingsForgetWifi() {
  if (!open()) return;
  prefs.remove("wifi_ssid");
  prefs.remove("wifi_pass");
}

uint8_t settingsIncrementBootCount() {
  if (!open()) return 0;
  const uint8_t count = static_cast<uint8_t>(prefs.getUChar("boot_cnt", 0) + 1);
  prefs.putUChar("boot_cnt", count);
  return count;
}

void settingsClearBootCount() {
  if (open()) prefs.putUChar("boot_cnt", 0);
}
```

- [ ] **Step 8: Rewrite `src/main.cpp`**

```cpp
#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>

#include "app_state.h"
#include "carrier.h"
#include "schedule.h"
#include "settings_store.h"
#include "time_sync.h"
#include "transmitter.h"
#include "version.h"

AppState app;

namespace {

const uint32_t kColdBootBroadcastSec = 10 * 60;
const uint32_t kNtpWaitMs = 20000;
const uint32_t kWifiConnectTimeoutMs = 20000;
const uint32_t kLoopPeriodMs = 25;
const uint32_t kFreqStepHz = 10;

bool setupMode = false;
bool coldBootPending = false;
bool stayAwakeLogged = false;

String formatLocal(time_t epoch) {
  struct tm t;
  localtime_r(&epoch, &t);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
  return String(buf);
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("jjy");
  WiFi.setAutoReconnect(true);
  WiFi.begin(app.settings.wifiSsid.c_str(), app.settings.wifiPass.c_str());
  Serial.printf("[WiFi] Connecting to %s", app.settings.wifiSsid.c_str());
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < kWifiConnectTimeoutMs) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] Connected, IP %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[WiFi] Not connected yet; retrying in the background.");
  }
}

void handleSerial() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    uint32_t hz = app.settings.freqHz;
    if (c == '+') {
      hz += kFreqStepHz;
    } else if (c == '-') {
      hz -= kFreqStepHz;
    } else if (c == '?') {
      Serial.printf("[PWM] %lu Hz (range %lu..%lu)\n", static_cast<unsigned long>(hz),
                    static_cast<unsigned long>(kFreqMinHz), static_cast<unsigned long>(kFreqMaxHz));
      continue;
    } else {
      continue;
    }
    if (!settingsValidFreq(hz)) {
      Serial.println("[PWM] Limit reached.");
      continue;
    }
    app.settings.freqHz = hz;
    carrierSetFrequency(hz);
    settingsSave(app.settings);
    Serial.printf("[PWM] %lu Hz\n", static_cast<unsigned long>(hz));
  }
}

bool sleepBlocked() {
  return app.uiHold || app.restartAtMs != 0 || app.otaState == OtaState::INSTALL_REQUESTED ||
         app.otaState == OtaState::INSTALLING;
}

void sleepUntil(time_t now, time_t wake) {
  Serial.printf("[SLEEP] Waking at %s (%ld s)\n", formatLocal(wake).c_str(), static_cast<long>(wake - now));
  Serial.flush();
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(wake - now) * 1000000ULL);
  esp_deep_sleep_start();
}

void runSchedule() {
  if (!timeSyncClockValid()) return;
  const time_t now = time(nullptr);

  if (coldBootPending) {
    coldBootPending = false;
    app.coldBootBroadcast = true;
    txStartWindow(now + kColdBootBroadcastSec);
    Serial.printf("[BOOT] Start-up broadcast until %s\n", formatLocal(now + kColdBootBroadcastSec).c_str());
    return;
  }

  if (txWindowActive()) {
    if (now < txWindowEnd()) return;
    txStopWindow();
    app.coldBootBroadcast = false;
    Serial.printf("[SCHED] Broadcast finished at %s\n", formatLocal(now).c_str());
  }

  time_t start;
  time_t end;
  if (scheduleCurrentWindow(app.settings.schedule, now, start, end)) {
    txStartWindow(end);
    stayAwakeLogged = false;
    Serial.printf("[SCHED] Broadcast %s -> %s\n", formatLocal(start).c_str(), formatLocal(end).c_str());
    return;
  }

  if (sleepBlocked()) return;

  time_t wake;
  if (!scheduleSleepUntil(app.settings.schedule, now, wake)) {
    if (!stayAwakeLogged) {
      Serial.println("[SCHED] Next broadcast is close; staying awake.");
      stayAwakeLogged = true;
    }
    return;
  }
  sleepUntil(now, wake);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\nJJY 40 kHz simulator %s\n", FW_VERSION);
  WiFi.persistent(false);

  settingsLoad(app.settings);
  timeSyncSetTz(app.settings.tzRule.c_str());
  carrierBegin(app.settings.freqHz);
  txBegin();

  if (app.settings.wifiSsid.empty()) {
    setupMode = true;
    Serial.println("[BOOT] No WiFi saved.");
    return;
  }

  const bool timerWake = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
  coldBootPending = !timerWake;
  connectWifi();
  timeSyncBegin(app.settings.tzRule.c_str());
  if (WiFi.status() == WL_CONNECTED && timeSyncWaitForNtp(kNtpWaitMs)) {
    Serial.printf("[NTP] Synced: %s\n", formatLocal(time(nullptr)).c_str());
  } else if (timeSyncClockValid()) {
    Serial.println("[NTP] No answer; using the clock kept during sleep.");
  } else {
    Serial.println("[NTP] No valid time yet; the carrier stays off until it arrives.");
  }
}

void loop() {
  handleSerial();
  if (app.restartAtMs != 0 && static_cast<int32_t>(millis() - app.restartAtMs) >= 0) ESP.restart();
  if (!setupMode) runSchedule();
  delay(kLoopPeriodMs);
}
```

- [ ] **Step 9: Remove `secrets.h`**

```bash
git rm include/secrets.h.example
rm -f include/secrets.h
```

Then edit `.gitignore` to delete the line `include/secrets.h`. Leave the other lines untouched.

- [ ] **Step 10: Build the firmware**

Run: `/mnt/c/Users/olafk/.platformio/penv/Scripts/pio.exe run -e esp32doit-devkit-v1`
Expected: `[SUCCESS]`, with no warnings from files under `src/` or `lib/jjy_core/`.

- [ ] **Step 11: Run all PC tests**

Run: `~/.venvs/pio/bin/pio test -e native`
Expected: all suites `PASSED`.

- [ ] **Step 12: Smoke-test on the device**

Flash it with `/mnt/c/Users/olafk/.platformio/penv/Scripts/pio.exe run -e esp32doit-devkit-v1 -t upload`, then ask the user to open the serial monitor (`! /mnt/c/Users/olafk/.platformio/penv/Scripts/pio.exe device monitor`).
Expected: `JJY 40 kHz simulator dev`, `[NVS] ... time zone warsaw ...` (migrated) and `[BOOT] No WiFi saved.`. There is no crash or reboot loop, and the `?` serial command prints the frequency.

- [ ] **Step 13: Commit**

```bash
git add src .gitignore
git commit -m "Split firmware into carrier, transmitter, time sync and settings modules"
```

---

### Task 7: Setup hotspot and setup page

**Files:**
- Create: `src/provisioning.h`, `src/provisioning.cpp`, `src/web_ui.h`, `src/web_ui.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `settingsIncrementBootCount`/`settingsClearBootCount`/`settingsForgetWifi`/`settingsSave` (Task 6), the HTML functions (Task 5), the settings rules (Task 4) and `app` (Task 6).
- Produces:
  - `provisioning.h`:
    - `bool provisioningResetGestureDetected();`
    - `void provisioningBootCounterTick();`
    - `void provisioningStartAp();`
    - `void provisioningLoop();`
    - `void provisioningScan();`
    - `const std::vector<std::string> &provisioningNetworks();`
    - `std::string provisioningApName();`
    - `enum class ConnectState : uint8_t { IDLE, CONNECTING, CONNECTED, FAILED };`
    - `void provisioningBeginConnect(const std::string &ssid, const std::string &pass);`
    - `ConnectState provisioningConnectState();`
    - `std::string provisioningStaIp();`
  - `web_ui.h`: `void webUiBeginSetup();`, `void webUiBeginNormal();` (implemented in Task 8) and `void webUiLoop();`

- [ ] **Step 1: Create `src/provisioning.h`**

```cpp
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
```

- [ ] **Step 2: Create `src/provisioning.cpp`**

```cpp
#include "provisioning.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <WiFi.h>
#include <esp_system.h>

#include <algorithm>

#include "settings_store.h"

namespace {

const uint8_t kResetPowerUps = 3;
const uint32_t kBootCounterWindowMs = 10000;
const uint32_t kConnectTimeoutMs = 20000;

DNSServer dns;
bool dnsRunning = false;
bool bootCounterPending = false;
std::vector<std::string> networks;
int strongestChannel = 1;
ConnectState connectState = ConnectState::IDLE;
uint32_t connectStartMs = 0;

}  // namespace

bool provisioningResetGestureDetected() {
  if (esp_reset_reason() != ESP_RST_POWERON) return false;
  const uint8_t count = settingsIncrementBootCount();
  Serial.printf("[BOOT] Power-up %u of %u for WiFi reset\n", static_cast<unsigned>(count),
                static_cast<unsigned>(kResetPowerUps));
  if (count >= kResetPowerUps) {
    settingsForgetWifi();
    settingsClearBootCount();
    return true;
  }
  bootCounterPending = true;
  return false;
}

void provisioningBootCounterTick() {
  if (bootCounterPending && millis() > kBootCounterWindowMs) {
    settingsClearBootCount();
    bootCounterPending = false;
  }
}

std::string provisioningApName() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char name[24];
  snprintf(name, sizeof(name), "JJY-Setup-%02X%02X", mac[4], mac[5]);
  return name;
}

void provisioningScan() {
  networks.clear();
  const int found = WiFi.scanNetworks();
  std::vector<int> order;
  for (int i = 0; i < found; ++i) order.push_back(i);
  std::sort(order.begin(), order.end(), [](int a, int b) { return WiFi.RSSI(a) > WiFi.RSSI(b); });
  if (!order.empty()) strongestChannel = WiFi.channel(order[0]);
  for (int i : order) {
    const std::string ssid = WiFi.SSID(i).c_str();
    if (ssid.empty()) continue;
    if (std::find(networks.begin(), networks.end(), ssid) == networks.end()) networks.push_back(ssid);
  }
  WiFi.scanDelete();
  Serial.printf("[SETUP] Found %u networks\n", static_cast<unsigned>(networks.size()));
}

const std::vector<std::string> &provisioningNetworks() { return networks; }

void provisioningStartAp() {
  WiFi.mode(WIFI_AP_STA);
  provisioningScan();
  const std::string name = provisioningApName();
  // Same channel as the strongest network, most likely the home router, so joining it later
  // doesn't move the hotspot and drop the phone.
  WiFi.softAP(name.c_str(), nullptr, strongestChannel);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());
  dnsRunning = true;
  Serial.printf("[SETUP] Hotspot %s on channel %d at %s\n", name.c_str(), strongestChannel,
                WiFi.softAPIP().toString().c_str());
}

void provisioningLoop() {
  if (dnsRunning) dns.processNextRequest();
  if (connectState != ConnectState::CONNECTING) return;
  if (WiFi.status() == WL_CONNECTED) {
    connectState = ConnectState::CONNECTED;
    Serial.printf("[SETUP] Connected, IP %s\n", WiFi.localIP().toString().c_str());
  } else if (millis() - connectStartMs > kConnectTimeoutMs) {
    WiFi.disconnect();
    connectState = ConnectState::FAILED;
    Serial.println("[SETUP] Could not connect.");
  }
}

void provisioningBeginConnect(const std::string &ssid, const std::string &pass) {
  WiFi.disconnect();
  WiFi.begin(ssid.c_str(), pass.empty() ? nullptr : pass.c_str());
  connectState = ConnectState::CONNECTING;
  connectStartMs = millis();
  Serial.printf("[SETUP] Trying %s\n", ssid.c_str());
}

ConnectState provisioningConnectState() { return connectState; }

std::string provisioningStaIp() {
  if (WiFi.status() != WL_CONNECTED) return std::string();
  return std::string(WiFi.localIP().toString().c_str());
}
```

- [ ] **Step 3: Create `src/web_ui.h`**

```cpp
#pragma once

void webUiBeginSetup();
void webUiBeginNormal();
void webUiLoop();
```

- [ ] **Step 4: Create `src/web_ui.cpp`** with the shared helpers and the setup routes

```cpp
#include "web_ui.h"

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include "app_state.h"
#include "html.h"
#include "provisioning.h"
#include "settings_rules.h"
#include "settings_store.h"

namespace {

const uint32_t kRestartAfterSetupMs = 30000;

WebServer server(80);
bool started = false;
bool setupMode = false;
DeviceSettings pending;
bool pendingValid = false;
bool pendingSaved = false;

std::string arg(const char *name) { return std::string(server.arg(name).c_str()); }

void sendHtml(const std::string &html) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html; charset=utf-8", String(html.c_str()));
}

void sendJson(const std::string &json) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", String(json.c_str()));
}

bool readTimezone(DeviceSettings &s, std::string &error) {
  s.tzId = arg("tz");
  const std::string custom = arg("tz_custom");
  std::string rule;
  if (settingsResolveTz(s.tzId, custom, rule)) {
    s.tzRule = rule;
    return true;
  }
  if (s.tzId == "custom") {
    s.tzRule = custom;
    error = "That custom time-zone rule doesn't look right.";
  } else {
    error = "Please pick a time zone.";
  }
  return false;
}

bool readSchedule(Schedule &out, std::string &error) {
  Schedule s = Schedule();
  for (int i = 0; i < server.args(); ++i) {
    if (server.argName(i) != "slot") continue;
    String value = server.arg(i);
    value.trim();
    if (value.length() == 0) continue;
    if (s.count >= kMaxSlots) {
      error = scheduleErrorText(ScheduleError::TOO_MANY);
      return false;
    }
    uint16_t minutes = 0;
    if (!parseHHMM(value.c_str(), minutes)) {
      error = scheduleErrorText(ScheduleError::OUT_OF_RANGE);
      return false;
    }
    s.slotMinutes[s.count++] = minutes;
  }
  const long window = server.arg("win").toInt();
  s.windowMinutes = (window > 0 && window <= 255) ? static_cast<uint8_t>(window) : 0;
  scheduleSort(s);
  out = s;
  const ScheduleError result = scheduleValidate(s);
  if (result != ScheduleError::OK) {
    error = scheduleErrorText(result);
    return false;
  }
  return true;
}

SetupPageView setupView(const std::string &error) {
  const DeviceSettings &s = pendingValid ? pending : app.settings;
  SetupPageView v;
  v.networks = provisioningNetworks();
  v.selectedSsid = s.wifiSsid;
  v.tzId = s.tzId;
  v.customTzRule = s.tzId == "custom" ? s.tzRule : "";
  v.schedule = s.schedule;
  v.autoDetectTz = !pendingValid;
  v.error = error;
  return v;
}

void handleSetupPage() {
  if (server.hasArg("rescan")) provisioningScan();
  const std::string error =
      server.hasArg("failed") ? "Couldn't connect. Check the network name and password, then try again." : "";
  sendHtml(renderSetupPage(setupView(error)));
}

void handleSetupSubmit() {
  DeviceSettings s = app.settings;
  s.wifiSsid = arg("ssid");
  if (s.wifiSsid.empty()) s.wifiSsid = arg("ssid_other");
  s.wifiPass = arg("pass");
  std::string wifiError;
  std::string tzError;
  std::string scheduleError;
  const bool wifiOk = settingsValidWifi(s.wifiSsid, s.wifiPass, wifiError);
  const bool tzOk = readTimezone(s, tzError);
  const bool scheduleOk = readSchedule(s.schedule, scheduleError);
  pending = s;
  pendingValid = true;
  pendingSaved = false;
  if (!wifiOk || !tzOk || !scheduleOk) {
    sendHtml(renderSetupPage(setupView(!wifiOk ? wifiError : (!tzOk ? tzError : scheduleError))));
    return;
  }
  provisioningBeginConnect(s.wifiSsid, s.wifiPass);
  sendHtml(renderConnectingPage());
}

void handleSetupStatus() {
  const ConnectState state = provisioningConnectState();
  const char *name = state == ConnectState::CONNECTED ? "connected"
                     : state == ConnectState::FAILED  ? "failed"
                                                      : "connecting";
  std::string json = "{\"state\":\"";
  json += name;
  json += "\",\"ip\":\"";
  json += jsonEscape(provisioningStaIp());
  json += "\"}";
  sendJson(json);
}

void handleCaptiveRedirect() {
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "");
}

}  // namespace

void webUiBeginSetup() {
  setupMode = true;
  server.on("/", HTTP_GET, handleSetupPage);
  server.on("/setup", HTTP_POST, handleSetupSubmit);
  server.on("/setup/status", HTTP_GET, handleSetupStatus);
  server.onNotFound(handleCaptiveRedirect);
  server.begin();
  started = true;
  Serial.println("[WEB] Setup page ready");
}

void webUiLoop() {
  if (!started) return;
  server.handleClient();
  if (setupMode && pendingValid && !pendingSaved && provisioningConnectState() == ConnectState::CONNECTED) {
    settingsSave(pending);
    pendingSaved = true;
    app.restartAtMs = millis() + kRestartAfterSetupMs;
    Serial.println("[SETUP] Settings saved; restarting in 30 s.");
  }
}
```

`webUiBeginNormal()` is declared but not defined until Task 8. Task 7 does not call it, so the build still links.

- [ ] **Step 5: Wire provisioning into `src/main.cpp`**

Add these includes after `#include "carrier.h"`:

```cpp
#include "provisioning.h"
#include "web_ui.h"
```

In `setup()`, replace

```cpp
  settingsLoad(app.settings);
```

with

```cpp
  settingsLoad(app.settings);
  if (provisioningResetGestureDetected()) {
    Serial.println("[BOOT] Three quick power-ups: WiFi forgotten.");
    app.settings.wifiSsid.clear();
    app.settings.wifiPass.clear();
  }
```

and replace

```cpp
    Serial.println("[BOOT] No WiFi saved.");
    return;
```

with

```cpp
    Serial.println("[BOOT] No WiFi saved; starting setup hotspot.");
    provisioningStartAp();
    webUiBeginSetup();
    return;
```

Replace the whole `loop()` with:

```cpp
void loop() {
  provisioningBootCounterTick();
  handleSerial();
  if (setupMode) provisioningLoop();
  webUiLoop();
  if (app.restartAtMs != 0 && static_cast<int32_t>(millis() - app.restartAtMs) >= 0) ESP.restart();
  if (!setupMode) runSchedule();
  delay(kLoopPeriodMs);
}
```

- [ ] **Step 6: Build**

Run: `/mnt/c/Users/olafk/.platformio/penv/Scripts/pio.exe run -e esp32doit-devkit-v1`
Expected: `[SUCCESS]`.

- [ ] **Step 7: Check the setup flow on the device** (the user does this with their phone)

Flash the firmware. NVS has no WiFi after Task 6, so the device starts the hotspot. Check each of these and note the results:
1. The serial log shows `[SETUP] Hotspot JJY-Setup-XXXX on channel N`.
2. Joining `JJY-Setup-XXXX` from a phone opens the setup page automatically. If it doesn't, browse to any http:// address, which should redirect.
3. The time zone is preselected to match the phone.
4. A wrong password shows "Couldn't connect…", and the time zone and schedule you entered are kept.
5. The right password shows "Done!" with an IP address. The log then shows `Settings saved; restarting in 30 s.`, and after the restart it connects, syncs NTP and starts the 10-minute broadcast.
6. Power-cycle three times within 10 s. The log shows `Power-up 3 of 3` and the hotspot returns. Then power-cycle once, wait more than 10 s, and power-cycle once more: the counter must not reach 3.

- [ ] **Step 8: Commit**

```bash
git add src/provisioning.h src/provisioning.cpp src/web_ui.h src/web_ui.cpp src/main.cpp
git commit -m "Add setup hotspot with captive portal and power-cycle WiFi reset"
```

---

### Task 8: Settings page on the home network

**Files:**
- Modify: `src/web_ui.cpp`, `src/main.cpp`

**Interfaces:**
- Consumes:
  - `txWindowActive`/`txStopWindow` (Task 6), `timeSyncSetTz`/`timeSyncClockValid` (Task 6), `carrierSetFrequency` (Task 6);
  - `scheduleNextStart`/`scheduleEquals` (Task 3), `renderSettingsPage`/`renderMessagePage` (Task 5);
  - `provisioningApName` (Task 7).
- Produces:
  - Routes: `GET /`, `POST /save`, `POST /forget` and `GET /status`.
  - `/status` returns JSON `{"version","time","broadcasting","ota","error"}`, where `ota` is `idle`, `updating` or `failed`.
  - mDNS `jjy.local`.

- [ ] **Step 1: Add the includes to `src/web_ui.cpp`** after `#include "settings_store.h"`:

```cpp
#include "carrier.h"
#include "schedule.h"
#include "time_sync.h"
#include "transmitter.h"
#include "version.h"
```

- [ ] **Step 2: Add the normal-mode handlers to `src/web_ui.cpp`**, inside the anonymous namespace just above `}  // namespace`:

```cpp
std::string formatLocalHHMM(time_t epoch) {
  struct tm t;
  localtime_r(&epoch, &t);
  char buf[8];
  strftime(buf, sizeof(buf), "%H:%M", &t);
  return buf;
}

void markUiActivity() {
  if (app.coldBootBroadcast && !app.uiHold) {
    app.uiHold = true;
    Serial.println("[WEB] Page opened during the start-up broadcast; sleep paused until Save.");
  }
}

SettingsPageView settingsView(const DeviceSettings &s, const std::string &notice, const std::string &error) {
  SettingsPageView v;
  if (timeSyncClockValid()) {
    const time_t now = time(nullptr);
    v.localTime = formatLocalHHMM(now);
    const time_t next = scheduleNextStart(app.settings.schedule, now);
    if (next != 0) v.nextBroadcast = formatLocalHHMM(next);
  }
  v.broadcasting = txWindowActive();
  v.tzId = s.tzId;
  v.customTzRule = s.tzId == "custom" ? s.tzRule : "";
  v.schedule = s.schedule;
  v.freqHz = s.freqHz;
  v.version = FW_VERSION;
  v.updateVersion = app.latestVersion;
  v.notice = notice;
  v.error = error;
  return v;
}

void handleSettingsPage() {
  markUiActivity();
  sendHtml(renderSettingsPage(settingsView(app.settings, "", "")));
}

void handleSave() {
  DeviceSettings s = app.settings;
  std::string tzError;
  std::string scheduleError;
  std::string error;
  const bool tzOk = readTimezone(s, tzError);
  const bool scheduleOk = readSchedule(s.schedule, scheduleError);
  bool ok = tzOk && scheduleOk;
  if (!ok) error = !tzOk ? tzError : scheduleError;
  if (ok && server.hasArg("freq")) {
    const long hz = server.arg("freq").toInt();
    if (hz <= 0 || !settingsValidFreq(static_cast<uint32_t>(hz))) {
      ok = false;
      error = "Carrier frequency must be between 39500 and 40500 Hz.";
    } else {
      s.freqHz = static_cast<uint32_t>(hz);
    }
  }
  if (!ok) {
    sendHtml(renderSettingsPage(settingsView(s, "", error)));
    return;
  }

  const bool timingChanged = s.tzRule != app.settings.tzRule || !scheduleEquals(s.schedule, app.settings.schedule);
  app.settings = s;
  settingsSave(s);
  timeSyncSetTz(s.tzRule.c_str());
  carrierSetFrequency(s.freqHz);
  if (timingChanged && txWindowActive() && !app.coldBootBroadcast) txStopWindow();
  app.uiHold = false;

  SettingsPageView v = settingsView(app.settings, "", "");
  v.notice = "Saved. ";
  if (!v.nextBroadcast.empty()) v.notice += "Next broadcast at " + v.nextBroadcast + ". ";
  v.notice += "The transmitter sleeps between broadcasts. To open this page again, unplug it and plug it back in.";
  sendHtml(renderSettingsPage(v));
}

void handleForget() {
  settingsForgetWifi();
  app.restartAtMs = millis() + 3000;
  sendHtml(renderMessagePage("WiFi forgotten", "The transmitter is restarting in setup mode. On your phone, join "
                                               "the WiFi network " +
                                                   provisioningApName() + " to set it up again."));
}

void handleStatus() {
  const char *ota = app.otaState == OtaState::FAILED ? "failed" : (app.otaState == OtaState::IDLE ? "idle" : "updating");
  std::string json = "{\"version\":\"";
  json += jsonEscape(FW_VERSION);
  json += "\",\"time\":\"";
  if (timeSyncClockValid()) json += formatLocalHHMM(time(nullptr));
  json += "\",\"broadcasting\":";
  json += txWindowActive() ? "true" : "false";
  json += ",\"ota\":\"";
  json += ota;
  json += "\",\"error\":\"";
  json += jsonEscape(app.otaError);
  json += "\"}";
  sendJson(json);
}
```

- [ ] **Step 3: Add `webUiBeginNormal()` to `src/web_ui.cpp`**, after `webUiBeginSetup()`:

```cpp
void webUiBeginNormal() {
  setupMode = false;
  server.on("/", HTTP_GET, handleSettingsPage);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/forget", HTTP_POST, handleForget);
  server.on("/status", HTTP_GET, handleStatus);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();
  started = true;
  Serial.println("[WEB] Settings page ready");
}
```

- [ ] **Step 4: Start the settings page and mDNS from `src/main.cpp`**

Add `#include <ESPmDNS.h>` after `#include <WiFi.h>`.

In the anonymous namespace, after `bool stayAwakeLogged = false;`, add:

```cpp
bool networkReady = false;

void serviceNetwork() {
  if (networkReady || WiFi.status() != WL_CONNECTED) return;
  networkReady = true;
  if (MDNS.begin("jjy")) MDNS.addService("http", "tcp", 80);
  Serial.printf("[WEB] Settings page: http://jjy.local (http://%s)\n", WiFi.localIP().toString().c_str());
}
```

In `setup()`, right after `connectWifi();`, add:

```cpp
  webUiBeginNormal();
```

In `loop()`, replace `if (!setupMode) runSchedule();` with:

```cpp
  if (!setupMode) {
    serviceNetwork();
    runSchedule();
  }
```

- [ ] **Step 5: Build**

Run: `/mnt/c/Users/olafk/.platformio/penv/Scripts/pio.exe run -e esp32doit-devkit-v1`
Expected: `[SUCCESS]`.

- [ ] **Step 6: Check on the device** (the user does this)

1. After a power-up, `http://jjy.local` opens on the phone (and the IP address works too). The page shows the time and "Broadcasting now", and the serial log shows `sleep paused until Save`.
2. Change the time zone to Tokyo and press Save. The notice appears, the page reloads with Tokyo selected, and the `[FRAME]` log lines switch to Tokyo time.
3. Add a start time 3 minutes from now with a 1-minute length and Save. When the start-up broadcast ends, the log shows the new window starting, then `[SLEEP] Waking at ...` for the next window minus 2 minutes.
4. An overlapping schedule, for example two times 2 minutes apart with a 5-minute length, shows the overlap error and doesn't save.
5. "Check for updates" returns 404 until Task 9. That's expected at this point.
6. "Forget WiFi" asks for confirmation, then the device restarts into the `JJY-Setup-XXXX` hotspot.

- [ ] **Step 7: Commit**

```bash
git add src/web_ui.cpp src/main.cpp
git commit -m "Add settings page on home network with mDNS"
```

---

### Task 9: Firmware updates from GitHub releases

**Files:**
- Create: `tools/make_certs_header.sh`, `src/certs.h` (generated), `src/ota.h`, `src/ota.cpp`
- Modify: `platformio.ini`, `src/web_ui.cpp`, `src/main.cpp`

**Interfaces:**
- Consumes: `versionCompare` (Task 4), `renderUpdatingPage` (Task 5), and `app.otaState`/`latestVersion`/`latestUrl`/`otaError` (Task 6).
- Produces:
  - `struct OtaRelease { std::string version; std::string url; };`
  - `bool otaFetchLatest(OtaRelease &out, std::string &error);`
  - `bool otaInstall(const std::string &url, std::string &error);`
  - `void otaMarkRunningImageValid();`
  - Routes `POST /update/check` and `POST /update/install`.

- [ ] **Step 1: Add ArduinoJson to the ESP32 environment in `platformio.ini`**

Under `[env:esp32doit-devkit-v1]`, add:

```ini
lib_deps =
    bblanchon/ArduinoJson@^7.2.0
```

- [ ] **Step 2: Create `tools/make_certs_header.sh`**

```sh
#!/bin/sh
# Regenerates src/certs.h from the system CA store (Debian/Ubuntu paths).
# api.github.com and github.com chain to USERTrust ECC / Sectigo E46;
# release downloads (release-assets.githubusercontent.com) chain to ISRG Root X1.
set -e
cd "$(dirname "$0")/.."
out=src/certs.h
{
  echo "#pragma once"
  echo ""
  echo "// Generated by tools/make_certs_header.sh. Root CAs for GitHub API and release downloads."
  echo "static const char kRootCerts[] ="
  for name in \
    USERTrust_ECC_Certification_Authority \
    USERTrust_RSA_Certification_Authority \
    Sectigo_Public_Server_Authentication_Root_E46 \
    Sectigo_Public_Server_Authentication_Root_R46 \
    ISRG_Root_X1 \
    ISRG_Root_X2 \
    DigiCert_Global_Root_G2; do
    echo "    // $name"
    sed 's/.*/    "&\\n"/' "/etc/ssl/certs/$name.pem"
  done
  echo "    ;"
} > "$out"
echo "Wrote $out"
```

- [ ] **Step 3: Generate the header and check it**

Run: `sh tools/make_certs_header.sh && grep -c "BEGIN CERTIFICATE" src/certs.h`
Expected: `Wrote src/certs.h`, then `7`.

- [ ] **Step 4: Create `src/ota.h`**

```cpp
#pragma once

#include <string>

struct OtaRelease {
  std::string version;
  std::string url;
};

bool otaFetchLatest(OtaRelease &out, std::string &error);
bool otaInstall(const std::string &url, std::string &error);
void otaMarkRunningImageValid();
```

- [ ] **Step 5: Create `src/ota.cpp`**

```cpp
#include "ota.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>

#include "certs.h"

namespace {
const char kLatestReleaseUrl[] = "https://api.github.com/repos/olafkrawczyk/jjy-simulator/releases/latest";
}

bool otaFetchLatest(OtaRelease &out, std::string &error) {
  WiFiClientSecure client;
  client.setCACert(kRootCerts);
  HTTPClient http;
  http.useHTTP10(true);  // no chunked encoding, so ArduinoJson can read the stream directly
  if (!http.begin(client, kLatestReleaseUrl)) {
    error = "Couldn't check for updates. Try again later.";
    return false;
  }
  http.setUserAgent("jjy-simulator");
  http.addHeader("Accept", "application/vnd.github+json");
  const int code = http.GET();
  if (code == HTTP_CODE_NOT_FOUND) {
    http.end();
    error = "No updates have been published yet.";
    return false;
  }
  if (code != HTTP_CODE_OK) {
    Serial.printf("[OTA] GitHub answered %d\n", code);
    http.end();
    error = "Couldn't check for updates. Try again later.";
    return false;
  }

  JsonDocument filter;
  filter["tag_name"] = true;
  filter["assets"][0]["name"] = true;
  filter["assets"][0]["browser_download_url"] = true;
  JsonDocument doc;
  const DeserializationError parseError =
      deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (parseError) {
    Serial.printf("[OTA] Parse error: %s\n", parseError.c_str());
    error = "Couldn't check for updates. Try again later.";
    return false;
  }

  const char *tag = doc["tag_name"] | "";
  out.url.clear();
  for (JsonObject asset : doc["assets"].as<JsonArray>()) {
    if (strcmp(asset["name"] | "", "firmware.bin") == 0) out.url = asset["browser_download_url"] | "";
  }
  if (*tag == '\0' || out.url.empty()) {
    error = "The latest release has no firmware file.";
    return false;
  }
  out.version = (tag[0] == 'v' || tag[0] == 'V') ? tag + 1 : tag;
  Serial.printf("[OTA] Latest release %s\n", out.version.c_str());
  return true;
}

bool otaInstall(const std::string &url, std::string &error) {
  WiFiClientSecure client;
  client.setCACert(kRootCerts);
  client.setTimeout(30);
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  httpUpdate.rebootOnUpdate(false);
  const t_httpUpdate_return result = httpUpdate.update(client, String(url.c_str()));
  if (result == HTTP_UPDATE_OK) return true;
  Serial.printf("[OTA] Update failed: %s\n", httpUpdate.getLastErrorString().c_str());
  error = "The update didn't finish. The current version is still installed. Try again later.";
  return false;
}

void otaMarkRunningImageValid() {
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
      state == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_ota_mark_app_valid_cancel_rollback();
    Serial.println("[OTA] New firmware confirmed.");
  }
}
```

- [ ] **Step 6: Add the update routes to `src/web_ui.cpp`**

Add these includes after `#include "version.h"`:

```cpp
#include "ota.h"
#include "version_compare.h"
```

Inside the anonymous namespace, after `handleStatus()`:

```cpp
void handleUpdateCheck() {
  markUiActivity();
  OtaRelease release;
  std::string error;
  if (!otaFetchLatest(release, error)) {
    sendHtml(renderSettingsPage(settingsView(app.settings, "", error)));
    return;
  }
  if (versionCompare(release.version.c_str(), FW_VERSION) > 0) {
    app.latestVersion = release.version;
    app.latestUrl = release.url;
    sendHtml(renderSettingsPage(settingsView(app.settings, "Version " + release.version + " is available.", "")));
    return;
  }
  app.latestVersion.clear();
  app.latestUrl.clear();
  sendHtml(renderSettingsPage(settingsView(app.settings, "You're up to date.", "")));
}

void handleUpdateInstall() {
  if (app.latestUrl.empty()) {
    sendHtml(renderSettingsPage(settingsView(app.settings, "", "Check for updates first.")));
    return;
  }
  app.otaState = OtaState::INSTALL_REQUESTED;
  app.otaError.clear();
  sendHtml(renderUpdatingPage(FW_VERSION));
}
```

In `webUiBeginNormal()`, add after the `/status` line:

```cpp
  server.on("/update/check", HTTP_POST, handleUpdateCheck);
  server.on("/update/install", HTTP_POST, handleUpdateInstall);
```

- [ ] **Step 7: Run installs and handle rollback in `src/main.cpp`**

Add `#include "ota.h"` after `#include "carrier.h"`.

Above `AppState app;`, add the hook that makes the Arduino core leave a new image pending until we confirm it:

```cpp
// Keep a freshly installed image in PENDING_VERIFY until it has reached the network;
// if it crashes before otaMarkRunningImageValid(), the bootloader rolls back.
extern "C" bool verifyRollbackLater() { return true; }
```

In `serviceNetwork()`, after `networkReady = true;`, add:

```cpp
  otaMarkRunningImageValid();
```

In `sleepUntil()`, insert as the first line:

```cpp
  otaMarkRunningImageValid();
```

In the anonymous namespace, after `serviceNetwork()`, add:

```cpp
void runOta() {
  if (app.otaState != OtaState::INSTALL_REQUESTED) return;
  app.otaState = OtaState::INSTALLING;
  txStopWindow();
  app.coldBootBroadcast = false;
  Serial.printf("[OTA] Installing %s\n", app.latestVersion.c_str());
  std::string error;
  if (otaInstall(app.latestUrl, error)) {
    Serial.println("[OTA] Installed; restarting.");
    delay(500);
    ESP.restart();
  }
  app.otaError = error;
  app.otaState = OtaState::FAILED;
}
```

In `loop()`, change the normal-mode block to:

```cpp
  if (!setupMode) {
    serviceNetwork();
    runOta();
    runSchedule();
  }
```

- [ ] **Step 8: Build and check the size**

Run: `/mnt/c/Users/olafk/.platformio/penv/Scripts/pio.exe run -e esp32doit-devkit-v1`
Expected: `[SUCCESS]`. The flash line must show less than `1310720` bytes used, because each app slot is 1.25 MB.

- [ ] **Step 9: Check on the device that TLS works** (the user does this)

With no releases published yet, press "Check for updates".
Expected: the page says "No updates have been published yet." This proves the TLS connection to `api.github.com` succeeded. If it says "Couldn't check…", read the serial log. A TLS error there means `src/certs.h` doesn't cover the chain.

- [ ] **Step 10: Commit**

```bash
git add platformio.ini tools/make_certs_header.sh src/certs.h src/ota.h src/ota.cpp src/web_ui.cpp src/main.cpp
git commit -m "Add firmware updates from GitHub releases with rollback protection"
```

---

### Task 10: Release workflow and README

**Files:**
- Create: `.github/workflows/release.yml`
- Modify: `README.md`

- [ ] **Step 1: Create `.github/workflows/release.yml`**

```yaml
name: Release

on:
  push:
    tags: ["v*"]

permissions:
  contents: write

jobs:
  release:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - uses: actions/setup-python@v5
        with:
          python-version: "3.12"

      - uses: actions/cache@v4
        with:
          path: ~/.platformio
          key: pio-${{ runner.os }}-${{ hashFiles('platformio.ini') }}

      - name: Install PlatformIO
        run: pip install platformio

      - name: Unit tests
        run: pio test -e native

      - name: Build firmware
        run: |
          VERSION="${GITHUB_REF_NAME#v}"
          export PLATFORMIO_BUILD_FLAGS="-DFW_VERSION=\\\"${VERSION}\\\""
          pio run -e esp32doit-devkit-v1
          cp .pio/build/esp32doit-devkit-v1/firmware.bin firmware.bin
          grep -c "${VERSION}" firmware.bin

      - name: Publish release
        env:
          GH_TOKEN: ${{ github.token }}
        run: gh release create "$GITHUB_REF_NAME" firmware.bin --title "$GITHUB_REF_NAME" --generate-notes
```

- [ ] **Step 2: Rewrite the README sections**

In `README.md`, replace everything from `## Configuration` up to (but not including) `## License` with:

```markdown
## Setting it up (no computer needed)

1. Plug the transmitter into a USB charger.
2. On your phone, join the WiFi network **JJY-Setup-XXXX**. A setup page opens by itself. If it doesn't, open any website and you'll be sent there.
3. Pick your home WiFi and type its password.
4. Pick the city you live in. Your clock will show that city's time.
5. Keep the suggested broadcast times unless you know when your clock checks the time, then press **Connect**.

When it says **Done!**, rejoin your home WiFi. You can change settings at **http://jjy.local**.

The transmitter sleeps between broadcasts. To open the settings page, unplug it and plug it back in: it stays awake for 10 minutes.

## Changing WiFi

- **From the settings page:** press **Forget WiFi**.
- **If it can't reach your WiFi any more** (new router or password): unplug and replug it **three times within 10 seconds**. The JJY-Setup network comes back. Your time zone and schedule are kept.

## Updating

On the settings page, press **Check for updates**, then **Install**. Don't unplug it while it updates (about a minute). If an update fails, the previous version keeps running.

## Features

- **Antiphase drive:** 40 kHz carrier on two pins in opposite phase, with dead time and a 5 ms soft ramp.
- **Phone setup:** open setup hotspot with a captive portal, so no app is needed.
- **Schedule:** up to 8 daily broadcast times with a shared length; deep sleep in between.
- **Time zones:** city list that preselects the phone's zone, plus custom POSIX rules.
- **Updates:** one-tap install from GitHub releases, with automatic rollback if a new version fails to start.

## Development

- Firmware: `pio run -e esp32doit-devkit-v1 -t upload`
- Unit tests (Linux/macOS/WSL): `pio test -e native`
- Serial commands at 115200 baud: `+` / `-` change the carrier by 10 Hz, `?` prints it.

## Releasing

Push a tag such as `v1.2.0`. GitHub Actions runs the tests, builds the firmware with that version number and publishes a release with `firmware.bin` attached. Devices see it under **Check for updates**.

If GitHub changes its certificate authorities, regenerate `src/certs.h` with `tools/make_certs_header.sh` on a Linux machine.
```

- [ ] **Step 3: Validate the workflow's YAML**

Run: `python3 -c "import yaml,sys; yaml.safe_load(open('.github/workflows/release.yml')); print('ok')"`
Expected: `ok`. If PyYAML is missing, run `~/.venvs/pio/bin/pip install pyyaml` first and use `~/.venvs/pio/bin/python`.

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/release.yml README.md
git commit -m "Add tag-triggered release workflow and rewrite README for phone setup"
```

---

### Task 11: Full manual check on the device

These checks happen on real hardware. The user does them with a phone, an iPhone and an Android phone if both are available, and the clock. Record each result in the PR description.

- [ ] **Step 1: Publish the first release.** Ask the user before pushing anything. Then:

```bash
git push origin main
git tag v1.0.0 && git push origin v1.0.0
```

Wait for the Release workflow to go green. Then check that `firmware.bin` is attached: `gh release view v1.0.0`.

- [ ] **Step 2: Flash a local `dev` build over USB and run the checklist from the spec:**
  - [ ] Setup from an iPhone: the captive page opens automatically and setup completes.
  - [ ] Setup from an Android phone: the same.
  - [ ] A wrong password shows the error and keeps the form.
  - [ ] A hidden network can be entered by name.
  - [ ] jjy.local opens the settings page. The IP fallback shown at setup works.
  - [ ] Unplugging and replugging once does not reset. Three times within 10 s opens the setup hotspot, with the time zone and schedule kept.
  - [ ] "Forget WiFi" restarts into the setup hotspot.
  - [ ] A schedule change takes effect. The device sleeps, wakes 2 minutes before the next window, and broadcasts the full window. The serial log shows a single "staying awake" line at most, not repeated lines.
  - [ ] After a timer wake, the log shows `[NTP] Synced` before the window starts.
  - [ ] The clock syncs.
  - [ ] OTA: the `dev` build finds v1.0.0, installs it, and `/status` reports version `1.0.0`. The log shows `[OTA] New firmware confirmed.`
  - [ ] OTA interrupted: publish `v1.0.1` (tag, wait for CI), start the install, unplug during the download, plug back in. The device boots v1.0.0.

- [ ] **Step 3: Fix anything that failed.** Use superpowers:systematic-debugging and commit each fix separately.
