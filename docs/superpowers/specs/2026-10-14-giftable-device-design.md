# Giftable JJY Device — Design

Date: 2026-10-14
Status: Approved in brainstorming, pending spec review

## Goal

Make the ESP32 JJY simulator usable by a non-technical person who cannot flash firmware and has no companion app. They must be able to:

1. Connect the device to their home WiFi.
2. Choose their time zone and the times their clock listens for the signal.
3. Install new firmware versions published on GitHub.
4. Recover from a WiFi change on a device with no buttons.

The owner flashes the first firmware over USB before handing the device over. Everything after that happens through a phone browser.

## Non-goals

- Bluetooth provisioning (Web Bluetooth does not work on iOS).
- Automatic fallback to the setup hotspot when saved WiFi fails.
- Automatic installation of updates.
- Per-window durations or day-of-week schedules.
- Migration to ESPAsyncWebServer (see Future work).

## 1. Boot flow, setup hotspot, reset

### Power-cycle reset counter

- Only boots with reset reason `ESP_RST_POWERON` count. Timer wake-ups and software restarts do not.
- On a counted boot, increment `boot_cnt` in NVS. After 10 s of uptime, write it back to 0.
- If `boot_cnt` reaches 3, erase the saved WiFi credentials, reset the counter, and enter setup mode. Time zone and schedule are kept.

### Setup mode (no WiFi saved)

- Open access point `JJY-Setup-XXXX`, where XXXX is the last 4 hex digits of the MAC address. No password.
- `DNSServer` answers every name with the AP address. These captive-portal probe URLs redirect to `/`: `/generate_204`, `/gen_204`, `/hotspot-detect.html`, `/library/test/success.html`, `/ncsi.txt`, `/connecttest.txt`, `/redirect`. Unknown paths also redirect to `/`.
- The setup page has these fields:
  - the network, picked from a scan list or typed in for hidden networks;
  - the password;
  - the time zone;
  - the schedule.
- On submit, run in AP+STA mode and try to connect for up to 20 s.
  - **Success:** save all settings and show "Done — on your home WiFi, open http://jjy.local (or http://<IP>)". Restart after 20 s.
  - **Failure:** show the form again with "Couldn't connect — check the network name and password." Nothing is saved.
- The carrier is off and deep sleep is disabled in setup mode. The AP stays up until setup completes.

### Normal mode (WiFi saved)

- The behaviour is unchanged otherwise: a cold boot runs a 10-minute broadcast, and the device deep-sleeps between scheduled windows, waking 2 minutes before each one.
- **After a timer wake:** wait up to 20 s for a real SNTP sync, detected with `sntp_set_time_sync_notification_cb` or `sntp_get_sync_status`. If no sync arrives, transmit on the RTC time kept during sleep and log it.
- **After a cold boot with WiFi failing:** keep retrying with the carrier off, because there is no valid time.
- **Forget WiFi** (settings page) erases the credentials and restarts into setup mode.

## 2. Settings and storage

The `settings` module is the only code that touches NVS. WiFi auto-persistence is disabled with `WiFi.persistent(false)`, so erasing the stored credentials really forgets the network.

| Key | Type | Default | Validation |
|---|---|---|---|
| `wifi_ssid`, `wifi_pass` | string | none | SSID 1–32 bytes; password empty or 8–63 bytes |
| `tz_id` | string | `warsaw` | must exist in table, or `custom` |
| `tz_rule` | string | `CET-1CEST,M3.5.0/2,M10.5.0/3` | non-empty, ≤ 64 chars, parses with `tzset` (custom only) |
| `slots` | blob, `uint16_t[1..8]`, minutes since midnight | 76, 256, 796, 976 (01:16, 04:16, 13:16, 16:16) | sorted, unique, 0..1439 |
| `win_min` | uint8 | 5 | 1..30; gap between consecutive starts, wrapping past midnight, ≥ `win_min` |
| `freq_hz` | uint32 | 40000 | 39500..40500 |
| `boot_cnt` | uint8 | 0 | n/a |

- Start times are minute-precise. Windows start at second :00.
- If two windows are closer than `win_min` + 2 min, the device stays awake between them instead of sleeping.
- Invalid input returns the form with a message and saves nothing.
- Migration on first boot of the new firmware:
  - If `tz_rule` exists without `tz_id`, set `tz_id` to `warsaw` when the rule equals the Warsaw rule, and to `custom` otherwise.
  - Erase `web_ovrd`, since keep-awake/override mode is removed.
  - `freq_hz` keeps its key and value.

### Time-zone table

- About 40 entries of `{id, label, iana, posix}`, covering major cities on every continent. Examples: Warsaw, London, Lisbon, Berlin, Helsinki, Moscow, Istanbul, Dubai, Delhi, Bangkok, Singapore, Tokyo, Sydney, Auckland, Honolulu, Los Angeles, Denver, Chicago, New York, São Paulo.
- The page preselects the entry whose `iana` matches `Intl.DateTimeFormat().resolvedOptions().timeZone`. Without a match it keeps the saved value, or Warsaw.
- A help line says: "Your clock will show the time of the city you pick."

## 3. Web UI

Replace the current UI completely: the Tailwind CDN, mode buttons, the status-JSON link and "Sleep now" all go. The aim is one clean page a non-technical person can use without instructions.

- Inline stylesheet of about 2 KB, system fonts, no external requests. It must work in the setup hotspot with no internet.
- Mobile-first, a single column, large tap targets.
- State changes use POST. GET only renders.
- Every value placed into HTML or JSON is escaped by the `html` module.

### Setup page (hotspot)

Title: "Set up your JJY clock transmitter". Sections in order:

1. **WiFi network:** a select filled from the scan, with an "Other…" option for typing a name, and the password.
2. **Time zone.**
3. **When does your clock check the time?** A list of start times with add and remove buttons (1–8 entries), and "Broadcast length" in minutes. A help line explains that radio clocks usually listen at night, so the defaults can stay if they don't know.
4. A "Connect" button.

### Settings page (http://jjy.local)

1. **Status card:** current local time, plus either "Next broadcast at 13:16" or "Broadcasting now".
2. **Time zone.**
3. **Schedule:** the same editor as on the setup page.
4. **Save** button. Saving stores and applies the settings, clears the cold-boot UI hold, and shows "Saved. The device sleeps until the next broadcast at HH:MM. To open this page again, unplug it and plug it back in."
5. **Device:**
   - the firmware version;
   - "Check for updates";
   - "Forget WiFi", which first asks for confirmation.
6. **Advanced** (collapsed `<details>`):
   - a custom POSIX time-zone rule;
   - the carrier frequency (Hz), which replaces the serial `+`/`-` commands. The serial commands stay for bench use.

`GET /status` stays as a JSON endpoint without any visible link. The update screen polls it.

## 4. Firmware updates

- **Version:**
  - `FW_VERSION` comes from a build flag. CI sets it from the tag (`v1.2.0` → `1.2.0`). Local builds default to `dev`.
  - `dev` is older than every release.
- **Check** (`POST /update/check`):
  - `GET https://api.github.com/repos/olafkrawczyk/jjy-simulator/releases/latest` with a `User-Agent` header.
  - ArduinoJson with a filter extracts `tag_name` and `assets[].{name, browser_download_url}`. Use the asset named `firmware.bin`.
  - Compare versions as semver. Show "Version X is available — Install" or "You're up to date".
  - On a network or parse error, show "Couldn't check for updates — try again later".
- **TLS:**
  - `WiFiClientSecure` with a bundle of long-lived root CAs in `certs.h`. It must cover `api.github.com` and the release-asset redirect host (`objects.githubusercontent.com` / `release-assets.githubusercontent.com`).
  - Check the actual chains at implementation time and include the roots for both.
- **Install** (`POST /update/install`):
  - Turn the carrier off and block sleep.
  - Download with `HTTPUpdate`, following redirects, into the inactive OTA slot. The update library checks the image before switching slots.
  - The page shows "Updating — don't unplug (about 1 minute)" and polls `/status` until the version changes.
  - A failed or interrupted download leaves the current version running.
- **Rollback:**
  - After a successful WiFi connection and web server start, call `esp_ota_mark_app_valid_cancel_rollback()`.
  - Whether the Arduino-ESP32 bootloader has rollback enabled is checked during implementation. If it doesn't, document that a release which crashes on boot must be recovered over USB.
- **Partitions:** set `board_build.partitions = default.csv` explicitly. It has two app slots of 1.25 MB, and the current image is 0.83 MB.
- **Release CI** (`.github/workflows/release.yml`), triggered by pushing a tag `v*`:
  1. Install PlatformIO.
  2. Run `pio test -e native`.
  3. Run `pio run -e esp32doit-devkit-v1` with `FW_VERSION` from the tag.
  4. Create the GitHub release with `firmware.bin` attached.
- **Remove `secrets.h`:** delete the include, `include/secrets.h.example`, the `.gitignore` line and the README section.

## 5. Code structure

`main.cpp` only wires the modules together and decides the boot mode.

| Module | Responsibility | Native-testable |
|---|---|---|
| `jjy_frame` | 60-symbol frame from `tm` | yes |
| `schedule` | current/next window, wake time, slot validation | yes |
| `settings` | `Settings` struct, NVS load/save, validation | validation |
| `timezones` | city table | table checks |
| `carrier` | MCPWM, ramp, on/off; single owner of carrier state | no |
| `transmitter` | 1 ms task turning the frame into carrier pulses | no |
| `provisioning` | AP, DNS, captive-portal routes, power-cycle counter, forget | no |
| `web_ui` | HTTP handlers; the only module that uses `WebServer` | no |
| `html` | page rendering and escaping | yes |
| `ota` | check/install, version compare, `certs.h` | version compare |

### Review fixes folded in

- After a timer wake, log "waiting for window" once instead of every 25 ms loop.
- After a timer wake, wait for a real SNTP sync (section 1).
- Make the ramp last its configured 5 ms. Integer division currently makes it 4 ms.
- Fix the race in `startTxWindow`: the transmitter reads a consistent window state, owned by `transmitter` and set through one function.
- Rewrite the README features, configuration and setup sections for the new flow.

## 6. Testing

### Native unit tests (`[env:native]`, Unity)

- `jjy_frame`: a known reference frame, including PA1/PA2 parity, day 366 and year rollover.
- `schedule`:
  - next window across midnight;
  - DST forward and backward in Warsaw;
  - overlap rejection;
  - close windows mean "stay awake";
  - wake time = window start − 2 min.
- `settings` validation: boundaries for every field.
- `ota`: semver compare, including `dev`.
- `html`: escaping `<>&"'`.
- `timezones`: unique IDs, and every POSIX rule loads with `setenv`/`tzset` and gives the expected offset at a fixed epoch.

### Manual device checklist

- [ ] Setup from an iPhone: the captive page opens automatically and setup completes.
- [ ] Setup from an Android phone: the same.
- [ ] Wrong password shows the error and keeps the form.
- [ ] A hidden network can be entered by name.
- [ ] jjy.local opens the settings page. The IP fallback shown at setup works.
- [ ] Unplugging and replugging once does not reset. Three times within 10 s opens the setup hotspot, and the time zone and schedule are kept.
- [ ] "Forget WiFi" restarts into the setup hotspot.
- [ ] A schedule change takes effect. The device sleeps and wakes 2 minutes before the next window and broadcasts the full window.
- [ ] The clock syncs.
- [ ] OTA: flash v1.0.0, publish v1.0.1, update from the UI, and the version changes.
- [ ] OTA interrupted by unplugging mid-download: the device still boots the old version.

## Future work

- Move the web layer to ESPAsyncWebServer. Keep `WebServer` usage confined to `web_ui` so this stays a contained swap.
