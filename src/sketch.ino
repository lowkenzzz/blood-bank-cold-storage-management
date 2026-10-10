/*
  Blood Bank Cold Storage Monitoring System
  ESP32 + DS18B20 + DHT22(sim) + door switch + RC522 RFID + 16x2 I2C LCD
  + buzzer + LEDs + local Wi-Fi dashboard.

  Academic embedded-systems proof-of-concept only.
  Not medically certified, not clinically validated, not for production use.

  Architecture: SENSE -> PROCESS -> DECIDE -> ACT
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHTesp.h>
#include <SPI.h>
#include <MFRC522.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include "logic.h"

/* ---------------- Pin map ---------------- */

#define PIN_DS18B20   27
#define PIN_DHT       25
#define PIN_DOOR      33   /* INPUT_PULLUP: LOW = CLOSED, HIGH = OPEN */
#define PIN_BUZZER    26
#define PIN_LED_GREEN 4
#define PIN_LED_RED   14

#define PIN_RFID_SS   5   /* SS/SDA=GPIO5; RST is moved to GPIO32 to keep LCD I2C on GPIO21/22. */
#define PIN_RFID_RST  32
/* SCK=18, MISO=19, MOSI=23 are the ESP32 VSPI pins. */

#define LCD_ADDRESS 0x27
#define LCD_COLS    16
#define LCD_ROWS    2

/* ---------------- Wi-Fi ---------------- */

const char *WIFI_SSID     = "Wokwi-GUEST";
const char *WIFI_PASSWORD = "";
const unsigned long WIFI_TIMEOUT_MS = 10000;

/* ---------------- Objects ---------------- */

OneWire oneWire(PIN_DS18B20);
DallasTemperature ds18b20(&oneWire);
DHTesp dhtSensor;
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLS, LCD_ROWS);
MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);
WebServer server(80);

/* ---------------- Runtime state ---------------- */

struct SystemState state;

const char *fefoCode     = "";
const char *fefoGroup    = "";
const char *fefoExpiry   = "";
const char *fefoLocation = "";

struct LcdPage pages[MAX_LCD_PAGES];
int pageCount   = 0;
int pageIndex   = 0;

unsigned long lastSensorMs = 0;
unsigned long lastPageMs   = 0;
unsigned long lastReportMs = 0;
unsigned long rfidLcdUntil = 0;

int lastTempStatus = -1;
int lastDoorOpen   = -1;
int lastDoorAlert  = -1;
int lastCritical   = -1;

const unsigned long SENSOR_PERIOD_MS = 1000;
const unsigned long PAGE_PERIOD_MS   = 2000;
const unsigned long REPORT_PERIOD_MS = 2000;

/* ---------------- Helpers ---------------- */

void showPage(int i) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(pages[i].line1);
  lcd.setCursor(0, 1);
  lcd.print(pages[i].line2);
}

void refreshLcd(bool forceFirstPage) {
  int previousCount = pageCount;
  pageCount = buildLcdPages(&state, fefoCode, pages, MAX_LCD_PAGES);
  if (pageCount == 0) return;
  if (forceFirstPage || pageCount != previousCount || pageIndex >= pageCount) {
    pageIndex = 0;
  }
  showPage(pageIndex);
}

void printStatusBlock() {
  Serial.print("TEMP: ");
  Serial.print(state.tempC, 1);
  Serial.println(" C");
  Serial.print("HUMIDITY: ");
  Serial.print(state.humidity);
  Serial.println(" %");
  Serial.println(tempStatusEvent(state.tempStatus));
  Serial.println(doorStatusEvent(state.doorOpen, state.doorAlert));
  Serial.println(systemStatusEvent(state.critical));
}

/* ---------------- Web dashboard ---------------- */

void sendStatusJson() {
  String json = "{";
  json += "\"temperature\":" + String(state.tempC, 1);
  json += ",\"humidity\":" + String(state.humidity);
  json += ",\"door\":\"" + String(state.doorOpen ? "OPEN" : "CLOSED") + "\"";
  json += ",\"tempCondition\":\"" + String(tempStatusEvent(state.tempStatus)) + "\"";
  json += ",\"doorCondition\":\"" + String(doorStatusEvent(state.doorOpen, state.doorAlert)) + "\"";
  json += ",\"systemStatus\":\"" + String(state.critical ? "CRITICAL" : "NORMAL") + "\"";
  json += ",\"lastUid\":\"" + String(state.lastUid) + "\"";

  if (state.lastUnitIndex >= 0) {
    int i = state.lastUnitIndex;
    json += ",\"lastUnit\":{\"code\":\"" + String(INVENTORY[i].code) + "\"";
    json += ",\"group\":\"" + String(INVENTORY[i].group) + "\"";
    json += ",\"expiry\":\"" + String(INVENTORY[i].expiryText) + "\"";
    json += ",\"location\":\"" + String(INVENTORY[i].location) + "\"}";
  } else {
    json += ",\"lastUnit\":null";
  }

  json += ",\"fefo\":{\"code\":\"" + String(fefoCode) + "\"";
  json += ",\"group\":\"" + String(fefoGroup) + "\"";
  json += ",\"expiry\":\"" + String(fefoExpiry) + "\"";
  json += ",\"location\":\"" + String(fefoLocation) + "\"}";

  json += ",\"inventory\":[";
  for (int i = 0; i < UNIT_COUNT; i++) {
    if (i > 0) json += ",";
    json += "{\"code\":\"" + String(INVENTORY[i].code) + "\"";
    json += ",\"group\":\"" + String(INVENTORY[i].group) + "\"";
    json += ",\"expiry\":\"" + String(INVENTORY[i].expiryText) + "\"";
    json += ",\"location\":\"" + String(INVENTORY[i].location) + "\"";
    json += ",\"uid\":\"" + String(INVENTORY[i].uid) + "\"}";
  }
  json += "]}";

  server.send(200, "application/json", json);
}

void sendDashboard() {
  /*
    Server-rendered dashboard.
    The browser auto-refreshes once per second, so no JavaScript is required.
    This avoids embedding JavaScript async/fetch syntax inside C++ source.
  */
  String html;
  html.reserve(12000);

  html += "<!DOCTYPE html><html><head>";
  html += "<meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<meta http-equiv='refresh' content='1'>";
  html += "<title>Blood Bank Cold Storage Monitor</title>";

  html += "<style>";
  html += "*{box-sizing:border-box}";
  html += "body{margin:0;font-family:Arial,Helvetica,sans-serif;background:#f3f6f9;color:#1f2937}";
  html += ".wrap{max-width:1200px;margin:auto;padding:24px}";
  html += ".header{background:#fff;border-radius:16px;padding:22px 24px;margin-bottom:18px;";
  html += "box-shadow:0 4px 14px rgba(0,0,0,.08);display:flex;justify-content:space-between;";
  html += "align-items:center;gap:16px;flex-wrap:wrap}";
  html += "h1{margin:0;font-size:26px}.sub{margin-top:5px;color:#64748b;font-size:14px}";
  html += ".live{font-size:13px;font-weight:700;color:#15803d;background:#dcfce7;padding:8px 12px;border-radius:999px}";
  html += ".alert{background:#fee2e2;border:1px solid #fca5a5;color:#991b1b;";
  html += "border-radius:14px;padding:16px 18px;margin-bottom:18px}";
  html += ".alert-title{font-size:18px;font-weight:700;margin-bottom:5px}";
  html += ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:16px;margin-bottom:18px}";
  html += ".card{background:#fff;border-radius:16px;padding:20px;box-shadow:0 4px 14px rgba(0,0,0,.08)}";
  html += ".title{font-size:12px;color:#64748b;font-weight:700;text-transform:uppercase;letter-spacing:.6px}";
  html += ".value{font-size:31px;font-weight:800;margin-top:9px}.normal{color:#15803d}.critical{color:#dc2626}";
  html += ".status{margin-top:7px;font-size:14px;font-weight:700}";
  html += ".cols{display:grid;grid-template-columns:1.15fr 1fr;gap:16px;margin-bottom:18px}";
  html += ".section{font-size:18px;font-weight:700;margin-bottom:14px}";
  html += ".box{background:#f8fafc;border-radius:12px;padding:16px}";
  html += ".uid{font-family:monospace;font-weight:700;font-size:17px;margin:6px 0 12px}";
  html += ".row{margin:6px 0;font-size:14px}.muted{color:#64748b}";
  html += ".unit{font-size:24px;font-weight:800;margin-bottom:5px}";
  html += ".fefo{background:#eff6ff;border:1px solid #bfdbfe;border-radius:12px;padding:18px}";
  html += ".fefo-code{font-size:30px;font-weight:800;color:#1d4ed8}.fefo-meta{margin-top:7px;color:#334155}";
  html += ".note{margin-top:9px;font-size:12px;color:#64748b}";
  html += ".tablewrap{overflow-x:auto}table{width:100%;border-collapse:collapse;min-width:720px}";
  html += "th{background:#f8fafc;color:#475569;text-transform:uppercase;font-size:12px;letter-spacing:.4px;padding:12px;text-align:left}";
  html += "td{padding:12px;border-top:1px solid #e5e7eb;font-size:14px}tr:hover{background:#f8fafc}";
  html += ".fefo-row{background:#eff6ff;font-weight:700}";
  html += ".footer{text-align:center;margin-top:18px;font-size:12px;color:#64748b}";
  html += "@media(max-width:800px){.cols{grid-template-columns:1fr}.wrap{padding:14px}h1{font-size:21px}}";
  html += "</style></head><body><div class='wrap'>";

  html += "<div class='header'><div><h1>Blood Bank Cold Storage Monitor</h1>";
  html += "<div class='sub'>ESP32 environmental monitoring and RFID inventory system</div></div>";
  html += "<div class='live'>● LIVE MONITORING</div></div>";

  if (state.critical) {
    html += "<div class='alert'><div class='alert-title'>CRITICAL CONDITION</div>";
    html += "<div>";

    if (state.tempStatus != TEMP_NORMAL) {
      html += String(tempStatusEvent(state.tempStatus));
    }

    if (state.tempStatus != TEMP_NORMAL && state.doorAlert) {
      html += " &nbsp;•&nbsp; ";
    }

    if (state.doorAlert) {
      html += String(doorStatusEvent(state.doorOpen, state.doorAlert));
    }

    html += "</div></div>";
  }

  /* Status cards */
  html += "<div class='grid'>";

  html += "<div class='card'><div class='title'>Temperature</div>";
  html += "<div class='value ";
  html += (state.tempStatus == TEMP_NORMAL) ? "normal" : "critical";
  html += "'>" + String(state.tempC, 1) + " C</div>";
  html += "<div class='status ";
  html += (state.tempStatus == TEMP_NORMAL) ? "normal" : "critical";
  html += "'>" + String(tempStatusEvent(state.tempStatus)) + "</div></div>";

  html += "<div class='card'><div class='title'>Humidity</div>";
  html += "<div class='value'>" + String(state.humidity) + " %</div>";
  html += "<div class='status muted'>ENVIRONMENTAL MONITORING</div></div>";

  html += "<div class='card'><div class='title'>Door</div>";
  html += "<div class='value ";
  html += state.doorAlert ? "critical" : "normal";
  html += "'>" + String(state.doorOpen ? "OPEN" : "CLOSED") + "</div>";
  html += "<div class='status ";
  html += state.doorAlert ? "critical" : "normal";
  html += "'>" + String(doorStatusEvent(state.doorOpen, state.doorAlert)) + "</div></div>";

  html += "<div class='card'><div class='title'>System Status</div>";
  html += "<div class='value ";
  html += state.critical ? "critical" : "normal";
  html += "'>" + String(state.critical ? "CRITICAL" : "NORMAL") + "</div>";
  html += "<div class='status ";
  html += state.critical ? "critical" : "normal";
  html += "'>";
  html += state.critical ? "IMMEDIATE ATTENTION REQUIRED" : "ALL MONITORED CONDITIONS NORMAL";
  html += "</div></div>";

  html += "</div>";

  /* RFID + FEFO */
  html += "<div class='cols'>";

  html += "<div class='card'><div class='section'>RFID Identification</div><div class='box'>";
  html += "<div class='muted'>LAST SCANNED UID</div>";
  html += "<div class='uid'>";
  html += (strlen(state.lastUid) > 0) ? String(state.lastUid) : "---";
  html += "</div>";

  if (state.lastUnitIndex >= 0) {
    int i = state.lastUnitIndex;
    html += "<div class='unit'>" + String(INVENTORY[i].code) + "</div>";
    html += "<div class='row'><span class='muted'>Blood Group:</span> ";
    html += String(INVENTORY[i].group) + "</div>";
    html += "<div class='row'><span class='muted'>Expiry:</span> ";
    html += String(INVENTORY[i].expiryText) + "</div>";
    html += "<div class='row'><span class='muted'>Location:</span> ";
    html += String(INVENTORY[i].location) + "</div>";
  } else if (strlen(state.lastUid) > 0) {
    html += "<div class='row'><span class='muted'>Result:</span> UNKNOWN RFID CARD</div>";
  } else {
    html += "<div class='row muted'>No RFID unit scanned yet.</div>";
  }

  html += "</div></div>";

  html += "<div class='card'><div class='section'>FEFO Priority</div><div class='fefo'>";
  html += "<div class='muted'>FIRST EXPIRY, FIRST OUT</div>";
  html += "<div class='fefo-code'>" + String(fefoCode) + "</div>";
  html += "<div class='fefo-meta'>" + String(fefoGroup) + " &nbsp;•&nbsp; " +
          String(fefoExpiry) + " &nbsp;•&nbsp; " + String(fefoLocation) + "</div>";
  html += "<div class='note'>Earliest valid expiry in the current demo inventory.</div>";
  html += "</div></div>";

  html += "</div>";

  /* Inventory */
  html += "<div class='card'><div class='section'>Stored Sample Inventory</div><div class='tablewrap'>";
  html += "<table><thead><tr><th>Unit</th><th>Blood Group</th><th>Expiry</th><th>Location</th><th>RFID UID</th></tr></thead><tbody>";

  int fefoIdx = fefoIndex();
  for (int i = 0; i < UNIT_COUNT; i++) {
    if (i == fefoIdx) html += "<tr class='fefo-row'>";
    else html += "<tr>";

    html += "<td>" + String(INVENTORY[i].code) + "</td>";
    html += "<td>" + String(INVENTORY[i].group) + "</td>";
    html += "<td>" + String(INVENTORY[i].expiryText) + "</td>";
    html += "<td>" + String(INVENTORY[i].location) + "</td>";
    html += "<td style='font-family:monospace'>" + String(INVENTORY[i].uid) + "</td>";
    html += "</tr>";
  }

  html += "</tbody></table></div></div>";

  html += "<div class='footer'>Academic prototype • Not medically certified • Updates every 1 second</div>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

/* ---------------- RFID ---------------- */

void handleRfid() {
  if (!rfid.PICC_IsNewCardPresent()) return;

  Serial.println("RFID CARD DETECTED");

  if (!rfid.PICC_ReadCardSerial()) {
    Serial.println("RFID READ FAILED");
    return;
  }

  char uid[32];
  formatUid(rfid.uid.uidByte, rfid.uid.size, uid, sizeof(uid));
  strncpy(state.lastUid, uid, sizeof(state.lastUid) - 1);
  state.lastUid[sizeof(state.lastUid) - 1] = '\0';
  state.lastUnitIndex = findUnitByUid(uid);

  Serial.print("RFID UID: ");
  Serial.println(state.lastUid);

  if (state.lastUnitIndex >= 0) {
    int i = state.lastUnitIndex;
    Serial.print("RFID UNIT: ");
    Serial.print(INVENTORY[i].code);
    Serial.print(" ");
    Serial.print(INVENTORY[i].group);
    Serial.print(" ");
    Serial.print(INVENTORY[i].expiryText);
    Serial.print(" ");
    Serial.println(INVENTORY[i].location);
  } else {
    Serial.println("RFID UNIT: UNKNOWN CARD");
  }

  Serial.print("FEFO: ");
  Serial.println(fefoCode);

  /* Keep the RFID result on the LCD immediately after a successful scan. */
  rfidLcdUntil = millis() + 5000UL;
  refreshLcd(true);

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

/* ---------------- Setup ---------------- */

void scanI2C() {
  Serial.println("I2C SCAN START");
  int found = 0;
  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission();
    if (error == 0) {
      Serial.print("I2C DEVICE: 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
      found++;
    }
  }
  if (found == 0) Serial.println("I2C DEVICE: NONE");
  Serial.println("I2C SCAN END");
}

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(PIN_DOOR, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED_GREEN, LOW);
  digitalWrite(PIN_LED_RED, LOW);

  systemInit(&state);

  int f = fefoIndex();
  if (f < 0) {
    /* Defensive fallback: with DEMO_DATE_MONTH/DAY set before every expiry in
       the demo inventory this cannot happen, but if the demo date or the
       inventory is edited so every unit is expired, fall back to unit 0
       instead of indexing with -1. */
    Serial.println("FEFO: NO VALID UNIT (all expired for the demo date)");
    f = 0;
  }
  fefoCode     = INVENTORY[f].code;
  fefoGroup    = INVENTORY[f].group;
  fefoExpiry   = INVENTORY[f].expiryText;
  fefoLocation = INVENTORY[f].location;

  Wire.begin(21, 22);
  scanI2C();
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("BLOOD BANK MON.");
  lcd.setCursor(0, 1);
  lcd.print("STARTING...");

  ds18b20.begin();
  dhtSensor.setup(PIN_DHT, DHTesp::DHT22);

  /*
    Explicit VSPI wiring:
      SCK  = GPIO18
      MISO = GPIO19
      MOSI = GPIO23
      SS   = GPIO5
  */
  SPI.begin(18, 19, 23, PIN_RFID_SS);
  pinMode(PIN_RFID_SS, OUTPUT);
  digitalWrite(PIN_RFID_SS, HIGH);

  pinMode(PIN_RFID_RST, OUTPUT);
  digitalWrite(PIN_RFID_RST, HIGH);

  rfid.PCD_Init();
  delay(50);

  /* Wokwi's MFRC522 should report a real version value after PCD_Init(). */
  byte rfidVersion = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  Serial.print("MFRC522 VERSION: 0x");
  Serial.println(rfidVersion, HEX);

  if (rfidVersion == 0x00 || rfidVersion == 0xFF) {
    Serial.println("MFRC522 STATUS: INIT FAILED");
  } else {
    Serial.println("MFRC522 STATUS: READY");
    rfid.PCD_SetAntennaGain(MFRC522::RxGain_max);
  }

  Serial.println("RFID READY: select a Wokwi card and press Tap.");

  /* Wi-Fi with a bounded wait: monitoring must work even without a network. */
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - wifiStart) < WIFI_TIMEOUT_MS) {
    delay(100);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WIFI CONNECTED: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WIFI NOT CONNECTED - LOCAL MONITORING ONLY");
  }

  server.on("/", sendDashboard);
  server.on("/api/status", sendStatusJson);
  server.begin();

  Serial.println("SYSTEM READY");
  Serial.print("FEFO: ");
  Serial.println(fefoCode);
}

/* ---------------- Loop ---------------- */

void loop() {
  unsigned long now = millis();

  server.handleClient();   /* never blocks the monitoring loop */
  handleRfid();

  if (now - lastSensorMs >= SENSOR_PERIOD_MS) {
    lastSensorMs = now;

    /* SENSE */
    ds18b20.requestTemperatures();
    float t = ds18b20.getTempCByIndex(0);
    if (t < -100) t = state.tempC;        /* ignore a bad read, keep last value */

    TempAndHumidity dhtData = dhtSensor.getTempAndHumidity();
    float hRaw = dhtData.humidity;
    int h = state.humidity;
    if (hRaw == hRaw) h = (int)hRaw;      /* NaN check without extra libraries */

    int doorOpen = (digitalRead(PIN_DOOR) == HIGH) ? 1 : 0;

    /* PROCESS + DECIDE */
    systemUpdate(&state, t, h, doorOpen, now);

    /* ACT */
    digitalWrite(PIN_LED_RED,   state.ledRed   ? HIGH : LOW);
    digitalWrite(PIN_LED_GREEN, state.ledGreen ? HIGH : LOW);

    /*
      Wokwi's buzzer is a piezoelectric buzzer. It needs an oscillating
      signal, so use tone()/noTone() rather than holding the pin HIGH.
    */
    static int lastBuzzerState = -1;
    if (state.buzzer != lastBuzzerState) {
      if (state.buzzer) {
        tone(PIN_BUZZER, 2200);
      } else {
        noTone(PIN_BUZZER);
      }
      lastBuzzerState = state.buzzer;
    }

    bool changed = (state.tempStatus != lastTempStatus) ||
                   (state.doorOpen   != lastDoorOpen)   ||
                   (state.doorAlert  != lastDoorAlert)  ||
                   (state.critical   != lastCritical);

    if (changed) {
      if (state.tempStatus != lastTempStatus) Serial.println(tempStatusEvent(state.tempStatus));
      if (state.doorOpen != lastDoorOpen || state.doorAlert != lastDoorAlert) {
        Serial.println(doorStatusEvent(state.doorOpen, state.doorAlert));
      }
      if (state.critical != lastCritical) Serial.println(systemStatusEvent(state.critical));

      lastTempStatus = state.tempStatus;
      lastDoorOpen   = state.doorOpen;
      lastDoorAlert  = state.doorAlert;
      lastCritical   = state.critical;

      refreshLcd(true);
    } else if (now >= rfidLcdUntil) {
      refreshLcd(false);
    }
  }

  if (now - lastReportMs >= REPORT_PERIOD_MS) {
    lastReportMs = now;
    printStatusBlock();
  }

  if (now - lastPageMs >= PAGE_PERIOD_MS) {
    lastPageMs = now;
    if (pageCount > 0 && now >= rfidLcdUntil) {
      pageIndex = (pageIndex + 1) % pageCount;
      showPage(pageIndex);
    }
  }
}
