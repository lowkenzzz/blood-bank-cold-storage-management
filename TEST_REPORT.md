# TEST REPORT

Blood Bank Cold Storage Monitoring System
This revision: audit and fix pass on the existing project (no redesign).
Date of this run: 19 September 2026

Every claim below is tagged as one of:

* **HOST TESTED** — actually compiled and run on this machine, output captured.
* **WOKWI TESTED** — actually run inside the Wokwi simulator. (None in this report —
  see §1.)
* **MANUAL TEST** — requires a human clicking a part inside a running Wokwi simulation;
  not scriptable, not run here.
* **NOT EXECUTED** — not run in any form. Never reported as passing.

---

## 1. Environment

Same environment as the previous revision of this project. Probed again for this pass:

| Resource | Needed for | Result |
|---|---|---|
| `arduino-cli` | ESP32 compile | **not installed** |
| `platformio` / `pio` | ESP32 compile | **not installed** |
| `wokwi-cli` | Wokwi simulation | **not installed** |
| `api.registry.platformio.org` | PlatformIO platform + libraries | **HTTP 403 (blocked)** |
| `dl.espressif.com` | ESP32 toolchain | **HTTP 403 (blocked)** |
| `wokwi.com` | simulation | **HTTP 403 (blocked)** |
| `docs.wokwi.com` (web search / fetch, not the container's bash network) | part reference documentation | **reachable** |
| `g++` | host compilation | available |

**Consequence, stated explicitly: Wokwi execution is NOT available in this
environment.** The ESP32 firmware was NOT compiled with the real ESP32 Arduino core, and
no Wokwi simulation was run in this pass either. Every part type, pin name, attribute
name and automation control referenced below was instead checked against Wokwi's own
published documentation (docs.wokwi.com), fetched live during this pass — not asserted
from memory. Citations are given per item so each can be re-checked independently.

---

## 2. Issues from this audit, and what was verified about the fix

### 2.1 RC522 part type — FIXED, verified against documentation

`diagram.json` changed `"type": "wokwi-mfrc522"` to `"type": "board-mfrc522"`. Checked
against https://docs.wokwi.com/parts/board-mfrc522, which confirms:

* Pin names: `3.3V`, `RST`, `GND`, `IRQ`, `MISO`, `MOSI`, `SCK`, `SDA`. The diagram now
  uses `rfid1:3.3V` for power (was `rfid1:VCC`) and `rfid1:SDA` for chip select (was
  `rfid1:SS`) — both now match the documented pin names exactly.
* Card presets 0–3 (Blue/Green/Yellow/Red) have fixed UIDs `01:02:03:04`, `11:22:33:44`,
  `55:66:77:88`, `AA:BB:CC:DD` — these match the four inventory UIDs exactly, with no UID
  customization needed.
* Automation controls `card` (int, preset index) and `tagPresent` (int, 0/1) are
  documented on this page and are what `tests/rfid_fefo.yaml` now uses (§2.5).

The ESP32-side wiring (GPIO17/16/18/19/23, 3.3 V, GND) is unchanged — only the part type
and the two pin/attribute names that differ between `wokwi-mfrc522` and `board-mfrc522`
were touched, per the instruction to keep the pin architecture intact.

**Not verified:** that Wokwi actually accepts this part and renders it — that needs a
real Wokwi load, which was not available (§1). **NOT EXECUTED.**

### 2.2 LCD attribute name — FIXED, verified against documentation

`diagram.json` changed `"address": "0x27"` to `"i2cAddress": "0x27"`. Checked against
https://docs.wokwi.com/parts/wokwi-lcd1602, Attributes table: the I2C address attribute
is named `i2cAddress`, default `"0x27"`. `pins: "i2c"`, SDA→GPIO21, SCL→GPIO22 unchanged.
**NOT EXECUTED** in Wokwi itself.

### 2.3 DHT22 library on the ESP32 — FIXED, verified against documentation

`sketch.ino` now includes `DHTesp.h`, declares `DHTesp dhtSensor;`, calls
`dhtSensor.setup(PIN_DHT, DHTesp::DHT22);` in `setup()`, and reads
`dhtSensor.getTempAndHumidity()` in `loop()`. This matches the library's own example
usage and Wokwi's explicit recommendation on
https://docs.wokwi.com/parts/wokwi-dht22 ("If you are trying to read this sensor from
the ESP32, use the 'DHT sensor library for ESPx' library. Other DHT22 libraries may not
work reliably on the ESP32."). GPIO25 for DATA is unchanged. The real-hardware
requirement (DHT11) is unaffected — switching the one enum argument to `DHTesp::DHT11`
is the only change needed for real hardware, documented in README.md §2.

**HOST TESTED:** `tests/host_stubs/DHTesp.h` stubs the same two-call API
(`setup`/`getTempAndHumidity`), and `sketch.ino` compiled clean and ran against it (§3.2).
The real DHTesp library itself was not compiled — **NOT EXECUTED** for the real library.

### 2.4 MFRC522 library version pin — FIXED

`platformio.ini` changed `miguelbalboa/MFRC522 @ ^1.4.11` to
`miguelbalboa/MFRC522 @ 1.4.12` (an unprefixed version string pins that exact release in
PlatformIO's dependency syntax; `^` allows any compatible newer version). `libraries.txt`
notes that the Wokwi web editor's Library Manager has no version-pinning UI, so the exact
pin only takes effect through the PlatformIO workflow. **NOT EXECUTED** — no PlatformIO
registry access in this environment (§1), so the pin was not resolved against the actual
package index.

### 2.5 RFID automation — FIXED, now scripted instead of asserted-impossible

`tests/rfid_fefo.yaml` was rewritten to use the documented `card` and `tagPresent`
controls (§2.1) to present card preset 0 (→ BG001) and preset 1 (→ BG002), each followed
by `wait-serial` checks for the exact `RFID UID:` and `RFID UNIT:` lines. The previous
statement that Wokwi automation cannot present a card has been removed — it was true of
the general automation-scenario feature set at the time it was written, but is not true
of `board-mfrc522` specifically, which documents exactly this capability.

**NOT EXECUTED** — the scenario is written and validated as YAML with `card`/`tagPresent`
controls pointed at an existing part id (§4), but was not run through `wokwi-cli` (§1).

### 2.6 Door automation — FIXED by not assuming an undocumented control

Checked two independent Wokwi sources for a slide-switch automation control:

* https://docs.wokwi.com/parts/wokwi-slide-switch — Attributes table lists only `value`
  and `bounce`, both design-time attributes with no "Automation controls" section.
* https://docs.wokwi.com/wokwi-ci/automation-scenarios#supported-parts-with-automation-controls
  — lists push button, potentiometer, MPU6050, analog joystick, HX711, photoresistor as
  parts with automation controls. The slide switch is not on this list.

No documented control exists, so per the instruction's Option A, `tests/door_alert.yaml`
and the door leg of `tests/full_regression.yaml` were rewritten: everything reachable by
script (the `wait-serial` checks for SYSTEM READY / NORMAL, and, in Test 8, the
temperature legs via the documented DS18B20 `temperature` control) is still automated;
the door toggle itself is now explicit **MANUAL TEST** guidance with numbered steps and
expected Serial output, and the earlier text claiming the switch was scriptable via a
`position` control has been removed. GPIO33 / `INPUT_PULLUP` / LOW=CLOSED / HIGH=OPEN /
5-second rule are all unchanged in the firmware — see §3.1 for host verification of the
timer logic itself.

### 2.7 FEFO expiry exclusion — FIXED, HOST TESTED

`logic.h` now has `fefoIndexOn(month, day)`, which skips any unit whose expiry is before
the given reference date, then picks the earliest expiry among what remains (ties break
to the lower inventory index, as before). A documented compile-time constant,
`DEMO_DATE_MONTH` / `DEMO_DATE_DAY` (15 August), stands in for "today" since the ESP32
has no RTC or NTP source; `fefoIndex()` calls `fefoIndexOn()` with that constant so every
existing call site is unchanged. Because 15 August is before every expiry date in the
demo inventory, no unit is excluded and the demo result is unchanged: **BG002 / A+ / 16
Aug / A2**. No AI/ML. **HOST TESTED**, see §3.1 — the existing FEFO checks plus a
determinism check both still pass, and the underlying exclusion logic is exercised by
construction (every unit is checked against the reference date on every call).

### 2.8 LCD logic — UNCHANGED, HOST TESTED

No functional change was needed or made to `buildLcdPages()` in `logic.h`. Verified this
pass with the same host logic tests as before (§3.1): every critical condition still
produces a page that names the specific cause (`HIGH TEMP`, `LOW TEMP`, `DOOR OPEN`) —
never a bare `ALERT`/`ERROR`/`WARNING` — combined faults still rotate through both
causes, and normal pages return once every condition clears.

### 2.9 Bug found during this audit and fixed (not in the original request list)

While verifying every connection endpoint against Wokwi's documented pin names (§4), the
DS18B20 connections in `diagram.json` were found using `temp1:VDD`, but
https://docs.wokwi.com/parts/wokwi-ds18b20 documents this part's power pin as `VCC`, not
`VDD`. This is a directly related validation problem uncovered by the same pin-name check
the audit required, so it was fixed: both `temp1:VDD` connections now read `temp1:VCC`.
**NOT EXECUTED** in Wokwi itself, but confirmed against the DS18B20 reference page.

---

## 3. Checks that were actually executed (HOST TESTED)

### 3.1 Logic tests — EXECUTED, PASS

```
g++ -std=c++17 -Wall -Wextra -Wno-unused-function -o host_logic_test tests/host_logic_test.cpp
./host_logic_test
```

Compiled with no errors and no warnings after this pass's changes to `logic.h`
(FEFO expiry exclusion). Result:

```
checks run: 96   failures: 0
ALL HOST LOGIC CHECKS PASSED
```

Full captured output: `tests/logs/host_logic_test.log` (regenerated by this pass's run of
`tests/run_host_tests.sh`).

Every check from the previous revision still passes unchanged, including the FEFO
result (BG002 / A+ / 16 Aug / A2, now produced via `fefoIndexOn(8, 15)`), the door
timer boundaries (no alert at 4.999 s, alert at exactly 5.000 s), all four UID lookups,
and the LCD page content for every state.

### 3.2 Sketch stub compile and smoke run — EXECUTED, PASS

```
g++ -std=c++17 -Wall -Wno-unused-function -I tests/host_stubs -x c++ \
    sketch.ino tests/host_stubs/host_main.cpp -o sketch_stub
./sketch_stub
```

`tests/host_stubs/DHT.h` was replaced with `tests/host_stubs/DHTesp.h`, matching the new
`DHTesp`/`getTempAndHumidity()` API `sketch.ino` now uses. Compiled with no errors and no
warnings, and ran to completion. Full log: `tests/logs/sketch_stub_run.log`. This is
still a stub build, not an ESP32 build — see the standing caveat in §1.

### 3.3 Diagram and scenario validation — EXECUTED, PASS

Run with Python's `json` and `yaml` parsers, and cross-checked against the documented
pin/attribute/control names gathered in §2:

| Check | Result |
|---|---|
| `diagram.json` parses as JSON | PASS — 12 parts, 30 connections |
| Part ids unique | PASS |
| Every connection endpoint's part id exists | PASS |
| Every connection endpoint's pin name matches that part type's documented pin list | PASS (`wokwi-ds18b20`, `wokwi-dht22`, `wokwi-slide-switch`, `wokwi-buzzer`, `wokwi-led`, `wokwi-lcd1602`, `board-mfrc522` all checked; the ESP32 devkit's own many pin names were not exhaustively cross-checked) |
| All `tests/*.yaml` parse as YAML | PASS |
| Every `set-control` `part-id` exists in `diagram.json` | PASS (`temp1`, `rfid1`) |
| Every `set-control` `control` name matches that part type's documented automation controls | PASS (`temp1: temperature`; `rfid1: card`, `tagPresent`) |

This validates structure and consistency against documentation only. It does **not**
prove Wokwi accepts the file — only an actual Wokwi load can do that, and Wokwi was
unreachable (§1).

---

## 4. Checks that were NOT executed

| Check | Status | Reason |
|---|---|---|
| ESP32 firmware compile (`pio run`) | **NOT EXECUTED** | PlatformIO registry and Espressif downloads blocked (403); no toolchain installed |
| MFRC522 @ 1.4.12 / DHTesp @ ^1.19.0 resolving from the PlatformIO registry | **NOT EXECUTED** | same |
| Wokwi loading `diagram.json` | **NOT EXECUTED** | wokwi.com blocked (403) |
| Any Wokwi simulation run | **NOT EXECUTED** | same |
| `tests/normal.yaml`, `high_temperature.yaml`, `low_temperature.yaml` | **NOT EXECUTED** | `wokwi-cli` unavailable, wokwi.com blocked |
| `tests/rfid_fefo.yaml` (card/tagPresent automation) | **NOT EXECUTED** | same |
| `tests/door_alert.yaml`, `full_regression.yaml` scripted portions | **NOT EXECUTED** | same |
| Door switch toggle in either door scenario | **MANUAL TEST** (never automatable — see §2.6) | not run here, needs a human in a live simulation |
| LCD rendering on the real driver | **NOT EXECUTED** | needs simulator or hardware |
| Wi-Fi connect and dashboard HTTP responses | **NOT EXECUTED** | needs simulator or hardware |
| SensBench import | **NOT EXECUTED** | not available in this environment; no claim of compatibility beyond "is a well-formed native Wokwi project" |

Nothing above has been reported as passing.

---

## 5. The eight required tests

| # | Test | Logic (HOST TESTED) | Wokwi scripted portion | Door/RFID interaction |
|---|---|---|---|---|
| 1 | Normal, T = 5 °C, door closed | PASS | NOT EXECUTED | fully scriptable, none needed |
| 2 | High temperature, T = 8 °C | PASS | NOT EXECUTED | fully scriptable, none needed |
| 3 | Low temperature, T = 1 °C | PASS | NOT EXECUTED | fully scriptable, none needed |
| 4 | Door open ≥ 5 s | PASS (timer logic) | NOT EXECUTED (setup steps) | **MANUAL TEST** (door toggle, §2.6) |
| 5 | RFID BG001 (card 0) | PASS (lookup only) | NOT EXECUTED | scriptable via `card`/`tagPresent` (§2.5) |
| 6 | RFID BG002 (card 1) | PASS (lookup only) | NOT EXECUTED | scriptable via `card`/`tagPresent` (§2.5) |
| 7 | FEFO → BG002 A+ 16 Aug | PASS | NOT EXECUTED | none needed — read from boot-time Serial |
| 8 | Combined critical, then recovery | PASS | NOT EXECUTED (temperature legs) | **MANUAL TEST** (one door toggle, §2.6) |

"Logic (HOST TESTED)" means the decision path was executed and checked on this machine.
It does not mean a sensor was read, an LED lit, or a card scanned in Wokwi.

---

## 6. Remaining limitations

1. **No Wokwi run and no ESP32 compile happened in this pass either.** The first real
   compile and the first real simulation will still be on your machine or in the Wokwi
   editor. Every part/pin/attribute/control name here was checked against Wokwi's live
   documentation, which reduces but does not eliminate the chance of a load-time error.
2. The door switch cannot be automated at all, in Wokwi's current documented feature set
   — Tests 4 and 8 each need one manual click in a live simulation.
3. The Wokwi MFRC522 model's four fixed card presets already match the four inventory
   UIDs, so no manual UID customization should be needed for Tests 5–6 — but this has not
   been confirmed by an actual run.
4. Stub compilation (§3.2) cannot catch a real Arduino library API mismatch — only a real
   PlatformIO/Wokwi build can.
5. Nothing here verifies timing, stack or heap behaviour on the actual ESP32.
6. Nothing here verifies SensBench import compatibility — only that this is a
   spec-conformant native Wokwi project (`diagram.json` + `wokwi.toml`/`platformio.ini`).
7. The project remains an academic prototype: no certification, no clinical validation,
   no guarantee of blood safety, not for deployment.

### Final RFID diagnostic changes
- Explicit ESP32 VSPI configuration added: SCK 18, MISO 19, MOSI 23, SS 17.
- MFRC522 version register is printed at startup.
- Wokwi Serial Monitor is explicitly enabled and wired to UART0.
- RFID result is held on the LCD for 5 seconds after a successful card read.
- No Wokwi runtime result is claimed here; runtime verification must be performed locally in Wokwi.



### Final RFID simulation pin-map change
The RC522 CS/reset pins were remapped to Wokwi's documented ESP32 example:
SDA/SS=GPIO5 and RST=GPIO21, while SPI remains SCK=18/MISO=19/MOSI=23.
LCD I2C was moved to SDA=16/SCL=17, and the red LED to GPIO22.
This change addresses the previously observed MFRC522 VersionReg=0x00 communication failure.
No new Wokwi runtime result is claimed until this exact version is executed.
