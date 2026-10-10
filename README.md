# Blood Bank Cold Storage Monitoring System

ESP32-based cold-storage monitoring and inventory-assistance prototype, packaged as a
native Wokwi project.

**Scope statement:** this is an academic embedded-systems proof-of-concept. It is not
medically certified, not clinically validated, does not guarantee blood safety, and is
not intended for production or clinical deployment.

Architecture: **SENSE → PROCESS → DECIDE → ACT**

---

## 1. Files

| File | Purpose |
|---|---|
| `diagram.json` | Native Wokwi circuit (this is the circuit file, not `circuit.json`) |
| `src/sketch.ino` | Main Arduino sketch: sensors, alerts, LCD, RFID, web server |
| `logic.h` | Pure decision logic: thresholds, door timer, FEFO, LCD page builder |
| `wokwi.toml` | Wokwi CLI / VS Code extension configuration |
| `platformio.ini` | PlatformIO build configuration for `esp32dev` |
| `libraries.txt` | Library list for the Wokwi web editor |
| `README.md` | This file |
| `TEST_REPORT.md` | What was actually executed, what was not |
| `tests/normal.yaml` | Test 1 — fully automated |
| `tests/high_temperature.yaml` | Test 2 — fully automated |
| `tests/low_temperature.yaml` | Test 3 — fully automated |
| `tests/door_alert.yaml` | Test 4 — partly automated, door toggle is manual (see §7) |
| `tests/rfid_fefo.yaml` | Tests 5–7 — fully automated via documented MFRC522 controls |
| `tests/full_regression.yaml` | Test 8 — partly automated, one door toggle is manual |
| `tests/host_logic_test.cpp` | Host test harness for `logic.h` |
| `tests/host_stubs/` | Arduino API stubs used to syntax-compile `src/sketch.ino` |
| `tests/run_host_tests.sh` | Runs both host checks |
| `tests/logs/` | Captured output of the host checks that were actually run |

`logic.h` exists so the same thresholds, the same door timer and the same FEFO routine
that run on the ESP32 can also be compiled and tested on a PC.

---

## 2. Hardware and simulation substitutions

| Function | Real hardware | Wokwi simulation part | Reason |
|---|---|---|---|
| Temperature | DS18B20 | `wokwi-ds18b20` | Same part |
| Humidity | **DHT11** | **`wokwi-dht22`** | Wokwi has no DHT11 model; DHT22 is the documented simulation substitute. The code is one enum value away from DHT11 (`DHTesp::DHT11` instead of `DHTesp::DHT22`) |
| Door sensor | **Magnetic reed switch** | **`wokwi-slide-switch`** | Wokwi has no reed-switch model; a slide switch reproduces the same open/closed contact. See §7 for why this part cannot be scripted |
| RFID | RC522 (MFRC522) | `board-mfrc522` | Wokwi's documented native RC522 part (see §6 for why it replaced the earlier `wokwi-mfrc522` type) |
| Alerts | Active buzzer, red + green LED | `wokwi-buzzer`, `wokwi-led` ×2 | — |
| Display | 16x2 I2C LCD @ 0x27 | `wokwi-lcd1602` (`pins: "i2c"`) | — |

To move to real DHT11 hardware, change `dhtSensor.setup(PIN_DHT, DHTesp::DHT22);` to
`DHTesp::DHT11` in `sketch.ino`. Nothing else changes: a reed switch wires exactly like
the slide switch.

---

## 3. Pin map

| Signal | ESP32 pin | Notes |
|---|---|---|
| DS18B20 DATA | GPIO27 | 4.7 kΩ pull-up between DATA and 3.3 V |
| DS18B20 VCC / GND | 3.3 V / GND | |
| DHT22 (sim) DATA | GPIO25 | VCC 3.3 V |
| Door switch | GPIO33 | `INPUT_PULLUP` |
| Buzzer | GPIO26 | Active buzzer, driven HIGH to sound |
| Green LED | GPIO4 | via 220 Ω |
| Red LED | GPIO5 | via 220 Ω |
| RC522 SCK | GPIO18 | |
| RC522 MISO | GPIO19 | |
| RC522 MOSI | GPIO23 | |
| RC522 SS (Wokwi calls this pin "SDA") | GPIO17 | |
| RC522 RST | GPIO16 | |
| RC522 power | **3.3 V** (Wokwi's `board-mfrc522` names this pin "3.3V") | Never 5 V — the RC522 is a 3.3 V device |
| LCD SDA | GPIO21 | I2C address 0x27 |
| LCD SCL | GPIO22 | |

Note on naming: the MFRC522 Arduino library and this sketch call the chip-select pin
"SS" (as in `MFRC522 rfid(SS_PIN, RST_PIN)`), while Wokwi's `board-mfrc522` part labels
the physical pin "SDA". Same wire, same GPIO17, two different names from two different
sources — this is not a wiring change from earlier revisions of this project, only a
naming difference to be aware of when reading `diagram.json`.

**Door polarity (explicit):**

* Switch common (pin 2) → GPIO33
* Switch pin 1 → GND → pin reads **LOW** → **DOOR CLOSED**
* Switch pin 3 → 3.3 V → pin reads **HIGH** → **DOOR OPEN**

With a real reed switch: magnet present (door shut) closes the contact to GND → LOW →
CLOSED. Removing the magnet releases the contact → the internal pull-up plus the 3.3 V
leg gives HIGH → OPEN.

---

## 4. Decision rules

**Temperature** (safe window 2 °C to 6 °C inclusive):

| Condition | State | Serial |
|---|---|---|
| `2 ≤ T ≤ 6` | NORMAL | `TEMP STATUS: NORMAL` |
| `T < 2` | CRITICAL LOW | `CRITICAL LOW TEMP` |
| `T > 6` | CRITICAL HIGH | `CRITICAL HIGH TEMP` |

**Door:** opening starts a timestamp. If the door stays continuously open for
**≥ 5000 ms**, `doorAlert` is set. Closing the door clears both the timer and the alert.
The 5-second rule is a `millis()` comparison inside `systemUpdate()` — there is no
`delay()` anywhere in the monitoring path, so sensors, RFID and the web server keep
running while the door timer counts.

**Act:** any critical condition → red LED ON, green LED OFF, buzzer ON, LCD switches to
critical pages, dashboard shows CRITICAL. No critical condition → green LED ON, red LED
OFF, buzzer OFF.

---

## 5. LCD behaviour (16x2, rotating pages, 2 s per page)

Normal rotation:

```
TEMP:   5.0 C        HUM:  45 %           RFID: BG001 O+
STATUS:NORMAL        DOOR: CLOSED         FEFO: BG002
```

Critical high temperature — the cause is named, never just "ALERT":

```
TEMP:   8.0 C        CRITICAL:
CRITICAL:            HIGH TEMP
```

Critical low temperature:

```
TEMP:   1.0 C        CRITICAL:
CRITICAL:            LOW TEMP
```

Door alert:

```
DOOR: OPEN           CRITICAL:
CRITICAL:            DOOR OPEN
```

Priority scheme: while any critical condition is active, **only** critical pages are
built, so temperature/humidity/inventory pages cannot hide an alarm. If temperature and
door are both critical, the LCD rotates through all four critical pages (temperature
cause first, then door cause). When everything clears, the normal pages return
automatically.

---

## 6. Inventory, RFID and FEFO

| Unit | Group | Expiry | Location | Card UID | Wokwi card preset |
|---|---|---|---|---|---|
| BG001 | O+ | 20 Aug | A1 | 01:02:03:04 | 0 — Blue Card |
| BG002 | A+ | 16 Aug | A2 | 11:22:33:44 | 1 — Green Card |
| BG003 | B+ | 25 Aug | B1 | 55:66:77:88 | 2 — Yellow Card |
| BG004 | AB+ | 30 Aug | B2 | AA:BB:CC:DD | 3 — Red Card |

On a scan the sketch reads the UID, normalises it to uppercase `XX:XX:XX:XX`, looks up
the unit, and prints:

```
RFID UID: 11:22:33:44
RFID UNIT: BG002 A+ 16 Aug A2
FEFO: BG002
```

An unrecognised card prints `RFID UNIT: UNKNOWN CARD` instead of guessing.

**RFID part:** the circuit uses Wokwi's documented native RC522 part, `board-mfrc522`
(https://docs.wokwi.com/parts/board-mfrc522). It ships four fixed card presets that
match this project's four inventory UIDs exactly (index 0–3 above), plus two more
presets (NFC Tag, Key Fob) not used here. `board-mfrc522` also documents two Automation
Scenario controls — `card` (selects a preset 0–5) and `tagPresent` (1 = present, 0 =
remove) — which `tests/rfid_fefo.yaml` uses to script Tests 5 and 6.

**FEFO** is a plain deterministic scan of the units that are not yet expired, for the
earliest `(month, day)`, with ties broken by the lower inventory index. No AI, no ML, no
randomness. Because the ESP32 in this prototype has no RTC or NTP source, "today" is the
documented compile-time constant `DEMO_DATE_MONTH` / `DEMO_DATE_DAY` in `logic.h` (15
August), set before every expiry date in the demo inventory so no unit is expired. For
this inventory and demo date the answer is always **BG002 / A+ / 16 Aug / A2**. Moving
the demo date past a unit's expiry excludes that unit from the selection.

---

## 7. Web dashboard

The ESP32 serves a local page on port 80 — no cloud, no external database.

* `GET /` — dashboard: temperature, humidity, door state, temperature condition, door
  condition, overall system status, last RFID UID, identified unit, FEFO result and the
  full inventory table. The page polls once per second.
* `GET /api/status` — the same data as JSON.

`server.handleClient()` is called every loop iteration and never blocks sensor or RFID
processing.

**Wi-Fi:** SSID `Wokwi-GUEST`, empty password. `setup()` waits at most 10 seconds. If the
connection fails it prints `WIFI NOT CONNECTED - LOCAL MONITORING ONLY` and carries on —
temperature, door, buzzer, LEDs, LCD and RFID all keep working without a network.

---

## 8. Serial events (used by the automation scenarios)

```
SYSTEM READY
TEMP STATUS: NORMAL
CRITICAL HIGH TEMP
CRITICAL LOW TEMP
DOOR STATUS: CLOSED
DOOR OPEN
CRITICAL DOOR OPEN
RFID UID: <uid>
RFID UNIT: <code> <group> <expiry> <location>
FEFO: BG002
SYSTEM STATUS: NORMAL
SYSTEM STATUS: CRITICAL
```

Each event is printed immediately on a state change, and the full status block is
reprinted every 2 seconds so a scenario that attaches late still sees the current state.

---

## 9. Why the door switch cannot be scripted

The real hardware uses a magnetic reed switch; Wokwi has no reed-switch model, so this
project substitutes `wokwi-slide-switch`. Two independent Wokwi references were checked
for a scriptable control on this part:

* The part's own reference page (https://docs.wokwi.com/parts/wokwi-slide-switch) lists
  only two **design-time attributes** — `value` (initial position) and `bounce` — and no
  "Automation controls" section.
* Wokwi's own automation scenarios guide
  (https://docs.wokwi.com/wokwi-ci/automation-scenarios#supported-parts-with-automation-controls)
  lists the parts that do support scripted automation controls (push button,
  potentiometer, MPU6050, analog joystick, HX711, photoresistor); the slide switch is not
  among them.

So there is no documented `set-control` that can flip the slide switch from a YAML
scenario. `tests/door_alert.yaml` and the door leg of `tests/full_regression.yaml`
therefore automate everything around the door check and call out the switch toggle as an
explicit manual step, rather than inventing an undocumented control name (an earlier
draft of this project incorrectly assumed a `position` control existed — that has been
removed). Everything else in this project — the DS18B20 `temperature` control and the
MFRC522 `card` / `tagPresent` controls — is scripted, because those controls **are**
documented on their respective part pages.

---

## 10. How to run it

### Wokwi web editor (simplest)

1. Go to wokwi.com and create a new ESP32 Arduino project.
2. Paste `sketch.ino` into the editor, and add `logic.h` as a second file with the same
   name.
3. Open `diagram.json` in the editor's diagram tab and replace its contents with this
   file's contents.
4. Open the Library Manager and add the libraries listed in `libraries.txt`.
5. Press play. Watch the Serial Monitor, the LCD and the LEDs.
6. Click the DS18B20 to change temperature; click the slide switch to open/close the
   door; click the RC522 to select and present a card.

### VS Code + PlatformIO + Wokwi extension

```bash
pio run                       # builds .pio/build/esp32dev/firmware.bin and .elf
# then: F1 -> "Wokwi: Start Simulator"
```

`wokwi.toml` already points at those two build artefacts. This is also the only workflow
that can pin MFRC522 to exactly 1.4.12, as `platformio.ini` specifies — the Wokwi web
editor's Library Manager does not expose version pinning.

### Automation scenarios (wokwi-cli, needs a Wokwi CI token)

```bash
pio run
wokwi-cli . --scenario tests/normal.yaml           --timeout 30000
wokwi-cli . --scenario tests/high_temperature.yaml --timeout 30000
wokwi-cli . --scenario tests/low_temperature.yaml  --timeout 30000
wokwi-cli . --scenario tests/door_alert.yaml       --timeout 30000   # door toggle is manual, see §9
wokwi-cli . --scenario tests/rfid_fefo.yaml        --timeout 30000
wokwi-cli . --scenario tests/full_regression.yaml  --timeout 60000   # one door toggle is manual, see §9
```

### Host checks (no Wokwi, no ESP32 needed)

```bash
bash tests/run_host_tests.sh
```

---

## 11. Known limitations

* The Wokwi MFRC522 model's built-in card presets (0–3) already match the four
  inventory UIDs exactly, so no manual UID editing should be needed — but this has not
  been confirmed inside an actual Wokwi run (see `TEST_REPORT.md`).
* The door switch cannot be driven from an automation scenario — see §9. Tests 4 and 8
  need one manual click each inside the running simulator.
* Expiry dates are stored as day + month only, with no year, which is enough for a demo
  inventory inside one month but not for real stock rotation. FEFO uses the documented
  `DEMO_DATE_MONTH` / `DEMO_DATE_DAY` constants in `logic.h` as its notion of "today"
  because the ESP32 has no RTC or NTP source in this build.
* Inventory is a fixed compile-time table in `logic.h`; there is no persistence, no
  issue/return workflow and no stock count.
* The dashboard has no authentication and runs over plain HTTP on the local network.
* The LCD is wired to 3.3 V. Many PCF8574 LCD backpacks want 5 V; on real hardware use
  the 5 V rail (VIN) for the LCD and keep the RC522 on 3.3 V.
* This is a SensBench-compatible native Wokwi project (a `diagram.json` plus a
  `wokwi.toml`/`platformio.ini` firmware build), but it has not been imported into or
  tested against SensBench itself.

## 8. Running the dashboard in Wokwi for VS Code

Start the simulation first. The ESP32 connects to `Wokwi-GUEST` and serves the dashboard
on HTTP port 80. `wokwi.toml` forwards that simulated port to the host at:

`http://localhost:8180/`

Open that address in Chrome/Edge/Firefox while the Wokwi simulation is running.

The dashboard refreshes `/api/status` every second and shows temperature, humidity, door
state, critical conditions, the last RFID UID, the matched unit's blood group/expiry/
location, FEFO selection, and the complete demo inventory.

## 9. Buzzer fix

The Wokwi buzzer is driven with `tone(2200)` / `noTone()` while a critical condition is
active. The earlier steady-HIGH method is not suitable for Wokwi's piezo buzzer. The
diagram sets the buzzer volume to 0.8 and mode to `smooth`.

## RFID / Serial Monitor troubleshooting (final)

The Wokwi Serial Monitor is explicitly enabled with `display: "always"` and ESP32 UART0
is connected to `$serialMonitor`.

On boot, verify:
`MFRC522 VERSION: 0x...`
`MFRC522 STATUS: READY`
`SYSTEM READY`

For RFID:
1. Keep the Wokwi simulation running.
2. Click the RC522 module.
3. Select Blue Card.
4. Press the RC522 control panel's **Tap** button (or enable Hold).
5. Look for:
   `RFID CARD DETECTED`
   `RFID UID: 01:02:03:04`
   `RFID UNIT: BG001 O+ 20 Aug A1`

The LCD holds the successful RFID result for 5 seconds before resuming its normal rotation.
The dashboard polls `/api/status` every second, so the Last RFID scan section updates automatically.



## Final Wokwi RFID pin mapping

The RC522 now uses Wokwi's documented ESP32 example mapping to eliminate the MFRC522
communication failure observed in simulation:

RC522:
- SDA/SS -> GPIO5
- RST -> GPIO21
- SCK -> GPIO18
- MISO -> GPIO19
- MOSI -> GPIO23
- 3.3V -> ESP32 3V3
- GND -> ESP32 GND

Because GPIO21 is now used by the RC522 reset line, the LCD I2C bus was moved to:
- LCD SDA -> GPIO16
- LCD SCL -> GPIO17

The red LED was moved to GPIO22.

The firmware uses:
`Wire.begin(16, 17);`

This mapping is intended to match the Wokwi-documented ESP32 MFRC522 configuration.
For physical hardware, verify the final GPIO map against the exact ESP32 board and wiring before assembly.


## Final LCD/RFID pin arrangement
- LCD SDA = GPIO21
- LCD SCL = GPIO22
- RFID SS/SDA = GPIO5
- RFID RST = GPIO32 (moved from GPIO21 so LCD can use standard I2C GPIO21/22)
- RFID SCK = GPIO18
- RFID MISO = GPIO19
- RFID MOSI = GPIO23
- Red LED = GPIO14

The LCD uses the ESP32 standard I2C pins. On boot the firmware scans the I2C bus and
prints `I2C DEVICE: 0x27` when the simulated LCD is detected.

### Dashboard implementation
The dashboard is server-rendered by the ESP32 and uses a 1-second HTML meta refresh.
There is no JavaScript in the firmware dashboard, avoiding C++ parsing issues while
still providing automatic live updates in the browser.

