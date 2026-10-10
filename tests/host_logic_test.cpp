/*
  host_logic_test.cpp

  Runs the decision logic of the Blood Bank Cold Storage Monitoring System
  on a normal PC, using a simulated millisecond clock.

  It includes ../logic.h, which is THE SAME FILE the ESP32 sketch includes,
  so these checks exercise the real thresholds, the real door timer, the real
  FEFO selection and the real LCD page builder.

  It does NOT test: ESP32 peripherals, Wi-Fi, the web server, the LCD driver,
  the DS18B20/DHT drivers or the MFRC522 driver. Those need Wokwi or hardware.

  Build: g++ -std=c++17 -Wall -o host_logic_test tests/host_logic_test.cpp
  Run:   ./host_logic_test
*/

#include "../logic.h"
#include <cstdio>
#include <cstring>

static int checks = 0;
static int failures = 0;

static void check(const char *name, bool ok) {
  checks++;
  if (!ok) failures++;
  printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
}

static void checkStr(const char *name, const char *got, const char *want) {
  bool ok = (strcmp(got, want) == 0);
  checks++;
  if (!ok) failures++;
  printf("  [%s] %s (got \"%s\", want \"%s\")\n", ok ? "PASS" : "FAIL", name, got, want);
}

/* Does any built page contain this text on either line? */
static bool pagesContain(const LcdPage *pages, int n, const char *needle) {
  for (int i = 0; i < n; i++) {
    if (strstr(pages[i].line1, needle) || strstr(pages[i].line2, needle)) return true;
  }
  return false;
}

static bool pagesFit16(const LcdPage *pages, int n) {
  for (int i = 0; i < n; i++) {
    if (strlen(pages[i].line1) > 16) return false;
    if (strlen(pages[i].line2) > 16) return false;
  }
  return true;
}

static void dumpPages(const LcdPage *pages, int n) {
  for (int i = 0; i < n; i++) {
    printf("      LCD page %d: |%-16s| |%-16s|\n", i + 1, pages[i].line1, pages[i].line2);
  }
}

int main() {
  const char *fefo = INVENTORY[fefoIndex()].code;
  LcdPage pages[MAX_LCD_PAGES];
  int n;

  printf("== Unit checks: temperature classification ==\n");
  check("2.0 C is NORMAL (inclusive lower bound)", classifyTemperature(2.0f) == TEMP_NORMAL);
  check("5.0 C is NORMAL",                          classifyTemperature(5.0f) == TEMP_NORMAL);
  check("6.0 C is NORMAL (inclusive upper bound)",  classifyTemperature(6.0f) == TEMP_NORMAL);
  check("1.0 C is LOW",                             classifyTemperature(1.0f) == TEMP_LOW);
  check("1.9 C is LOW",                             classifyTemperature(1.9f) == TEMP_LOW);
  check("6.1 C is HIGH",                            classifyTemperature(6.1f) == TEMP_HIGH);
  check("8.0 C is HIGH",                            classifyTemperature(8.0f) == TEMP_HIGH);

  printf("== Unit checks: RFID UID handling ==\n");
  {
    unsigned char raw[4] = { 0x01, 0x02, 0x03, 0x04 };
    char uid[32];
    formatUid(raw, 4, uid, sizeof(uid));
    checkStr("formatUid(01 02 03 04)", uid, "01:02:03:04");

    unsigned char raw2[4] = { 0xAA, 0xBB, 0xCC, 0xDD };
    formatUid(raw2, 4, uid, sizeof(uid));
    checkStr("formatUid(AA BB CC DD)", uid, "AA:BB:CC:DD");

    char norm[32];
    normalizeUid("aa:bb:cc:dd", norm, sizeof(norm));
    checkStr("normalizeUid lowercase input", norm, "AA:BB:CC:DD");

    check("01:02:03:04 -> BG001", findUnitByUid("01:02:03:04") == 0);
    check("11:22:33:44 -> BG002", findUnitByUid("11:22:33:44") == 1);
    check("55:66:77:88 -> BG003", findUnitByUid("55:66:77:88") == 2);
    check("AA:BB:CC:DD -> BG004", findUnitByUid("AA:BB:CC:DD") == 3);
    check("unknown card -> -1",   findUnitByUid("DE:AD:BE:EF") == -1);
  }

  printf("== Unit checks: FEFO ==\n");
  {
    int f = fefoIndex();
    checkStr("FEFO unit code",  INVENTORY[f].code,       "BG002");
    checkStr("FEFO blood group", INVENTORY[f].group,     "A+");
    checkStr("FEFO expiry",      INVENTORY[f].expiryText, "16 Aug");
    checkStr("FEFO location",    INVENTORY[f].location,   "A2");
    int again = fefoIndex();
    check("FEFO is deterministic across calls", again == f);
  }

  printf("== TEST 1: Normal, T = 5 C, door closed ==\n");
  {
    SystemState s; systemInit(&s);
    systemUpdate(&s, 5.0f, 45, 0, 1000);
    n = buildLcdPages(&s, fefo, pages, MAX_LCD_PAGES);
    dumpPages(pages, n);
    check("system not critical", s.critical == 0);
    check("green LED ON",        s.ledGreen == 1);
    check("red LED OFF",         s.ledRed == 0);
    check("buzzer OFF",          s.buzzer == 0);
    checkStr("serial temp event",   tempStatusEvent(s.tempStatus), "TEMP STATUS: NORMAL");
    checkStr("serial door event",   doorStatusEvent(s.doorOpen, s.doorAlert), "DOOR STATUS: CLOSED");
    checkStr("serial system event", systemStatusEvent(s.critical), "SYSTEM STATUS: NORMAL");
    check("LCD shows the temperature value", pagesContain(pages, n, "5.0 C"));
    check("LCD shows NORMAL",                pagesContain(pages, n, "NORMAL"));
    check("all LCD lines fit 16 chars",      pagesFit16(pages, n));
  }

  printf("== TEST 2: High temperature, T = 8 C ==\n");
  {
    SystemState s; systemInit(&s);
    systemUpdate(&s, 8.0f, 45, 0, 1000);
    n = buildLcdPages(&s, fefo, pages, MAX_LCD_PAGES);
    dumpPages(pages, n);
    check("temperature classified HIGH", s.tempStatus == TEMP_HIGH);
    check("system critical",             s.critical == 1);
    check("red LED ON",                  s.ledRed == 1);
    check("green LED OFF",               s.ledGreen == 0);
    check("buzzer ON",                   s.buzzer == 1);
    checkStr("serial event", tempStatusEvent(s.tempStatus), "CRITICAL HIGH TEMP");
    check("LCD names HIGH TEMP as the cause", pagesContain(pages, n, "HIGH TEMP"));
    check("LCD still shows the temperature",  pagesContain(pages, n, "8.0 C"));
    check("LCD does not fall back to a bare page", pagesContain(pages, n, "CRITICAL:"));
    check("all LCD lines fit 16 chars", pagesFit16(pages, n));
  }

  printf("== TEST 3: Low temperature, T = 1 C ==\n");
  {
    SystemState s; systemInit(&s);
    systemUpdate(&s, 1.0f, 45, 0, 1000);
    n = buildLcdPages(&s, fefo, pages, MAX_LCD_PAGES);
    dumpPages(pages, n);
    check("temperature classified LOW", s.tempStatus == TEMP_LOW);
    check("red LED ON",                 s.ledRed == 1);
    check("buzzer ON",                  s.buzzer == 1);
    checkStr("serial event", tempStatusEvent(s.tempStatus), "CRITICAL LOW TEMP");
    check("LCD names LOW TEMP as the cause", pagesContain(pages, n, "LOW TEMP"));
    check("LCD does not show HIGH TEMP",     !pagesContain(pages, n, "HIGH TEMP"));
    check("all LCD lines fit 16 chars",      pagesFit16(pages, n));
  }

  printf("== TEST 4: Door open >= 5 s, temperature normal ==\n");
  {
    SystemState s; systemInit(&s);
    systemUpdate(&s, 5.0f, 45, 0, 1000);      /* closed */
    check("no alert while closed", s.doorAlert == 0);

    systemUpdate(&s, 5.0f, 45, 1, 2000);      /* door opens at t=2000 */
    check("door reported OPEN immediately", s.doorOpen == 1);
    check("no alert yet at 0 s",            s.doorAlert == 0);
    checkStr("serial event at 0 s", doorStatusEvent(s.doorOpen, s.doorAlert), "DOOR OPEN");

    systemUpdate(&s, 5.0f, 45, 1, 6999);      /* 4.999 s open */
    check("no alert at 4.999 s", s.doorAlert == 0);
    check("not critical at 4.999 s", s.critical == 0);

    systemUpdate(&s, 5.0f, 45, 1, 7000);      /* exactly 5.000 s open */
    check("alert raised at exactly 5.000 s", s.doorAlert == 1);
    check("system critical", s.critical == 1);
    check("red LED ON",      s.ledRed == 1);
    check("green LED OFF",   s.ledGreen == 0);
    check("buzzer ON",       s.buzzer == 1);
    checkStr("serial event", doorStatusEvent(s.doorOpen, s.doorAlert), "CRITICAL DOOR OPEN");

    n = buildLcdPages(&s, fefo, pages, MAX_LCD_PAGES);
    dumpPages(pages, n);
    check("LCD names DOOR OPEN as the cause", pagesContain(pages, n, "DOOR OPEN"));
    check("all LCD lines fit 16 chars",       pagesFit16(pages, n));

    systemUpdate(&s, 5.0f, 45, 0, 9000);      /* door closes */
    check("alert cleared when door closes", s.doorAlert == 0);
    check("system back to normal",          s.critical == 0);
    check("green LED ON again",             s.ledGreen == 1);
    check("buzzer OFF again",               s.buzzer == 0);
  }

  printf("== TEST 4b: Door flicker must restart the timer ==\n");
  {
    SystemState s; systemInit(&s);
    systemUpdate(&s, 5.0f, 45, 1, 1000);   /* open  */
    systemUpdate(&s, 5.0f, 45, 1, 4000);   /* 3 s   */
    systemUpdate(&s, 5.0f, 45, 0, 4500);   /* closed, timer reset */
    systemUpdate(&s, 5.0f, 45, 1, 5000);   /* open again */
    systemUpdate(&s, 5.0f, 45, 1, 9000);   /* only 4 s since reopen */
    check("no alert 4 s after reopening", s.doorAlert == 0);
    systemUpdate(&s, 5.0f, 45, 1, 10000);  /* 5 s since reopen */
    check("alert 5 s after reopening", s.doorAlert == 1);
  }

  printf("== TEST 5: RFID card 01:02:03:04 ==\n");
  {
    SystemState s; systemInit(&s);
    systemUpdate(&s, 5.0f, 45, 0, 1000);
    s.lastUnitIndex = findUnitByUid("01:02:03:04");
    strcpy(s.lastUid, "01:02:03:04");
    check("card recognised", s.lastUnitIndex >= 0);
    checkStr("unit",     INVENTORY[s.lastUnitIndex].code,       "BG001");
    checkStr("group",    INVENTORY[s.lastUnitIndex].group,      "O+");
    checkStr("expiry",   INVENTORY[s.lastUnitIndex].expiryText, "20 Aug");
    checkStr("location", INVENTORY[s.lastUnitIndex].location,   "A1");
    n = buildLcdPages(&s, fefo, pages, MAX_LCD_PAGES);
    dumpPages(pages, n);
    check("LCD inventory page shows BG001", pagesContain(pages, n, "BG001"));
    check("all LCD lines fit 16 chars",     pagesFit16(pages, n));
  }

  printf("== TEST 6: RFID card 11:22:33:44 ==\n");
  {
    SystemState s; systemInit(&s);
    systemUpdate(&s, 5.0f, 45, 0, 1000);
    s.lastUnitIndex = findUnitByUid("11:22:33:44");
    strcpy(s.lastUid, "11:22:33:44");
    checkStr("unit",     INVENTORY[s.lastUnitIndex].code,       "BG002");
    checkStr("group",    INVENTORY[s.lastUnitIndex].group,      "A+");
    checkStr("expiry",   INVENTORY[s.lastUnitIndex].expiryText, "16 Aug");
    checkStr("location", INVENTORY[s.lastUnitIndex].location,   "A2");
  }

  printf("== TEST 7: FEFO result for the demo inventory ==\n");
  {
    int f = fefoIndex();
    printf("      FEFO -> %s %s %s %s\n", INVENTORY[f].code, INVENTORY[f].group,
           INVENTORY[f].expiryText, INVENTORY[f].location);
    check("FEFO selects BG002 / A+ / 16 Aug",
          strcmp(INVENTORY[f].code, "BG002") == 0 &&
          strcmp(INVENTORY[f].group, "A+") == 0 &&
          strcmp(INVENTORY[f].expiryText, "16 Aug") == 0);
  }

  printf("== TEST 8: Combined critical, then recovery ==\n");
  {
    SystemState s; systemInit(&s);
    systemUpdate(&s, 8.0f, 45, 1, 1000);    /* hot AND door just opened */
    check("temperature critical immediately", s.tempStatus == TEMP_HIGH);
    check("door alert not yet raised",        s.doorAlert == 0);

    systemUpdate(&s, 8.0f, 45, 1, 6000);    /* 5 s of open door */
    check("temperature critical", s.tempStatus == TEMP_HIGH);
    check("door critical",        s.doorAlert == 1);
    check("system critical",      s.critical == 1);
    check("red LED ON",           s.ledRed == 1);
    check("buzzer ON",            s.buzzer == 1);

    n = buildLcdPages(&s, fefo, pages, MAX_LCD_PAGES);
    dumpPages(pages, n);
    check("LCD rotation names HIGH TEMP", pagesContain(pages, n, "HIGH TEMP"));
    check("LCD rotation names DOOR OPEN", pagesContain(pages, n, "DOOR OPEN"));
    check("both causes fit in the page budget", n == 4);
    check("all LCD lines fit 16 chars", pagesFit16(pages, n));

    systemUpdate(&s, 5.0f, 45, 0, 9000);    /* restore */
    check("recovered: not critical", s.critical == 0);
    check("recovered: green LED ON", s.ledGreen == 1);
    check("recovered: red LED OFF",  s.ledRed == 0);
    check("recovered: buzzer OFF",   s.buzzer == 0);
    n = buildLcdPages(&s, fefo, pages, MAX_LCD_PAGES);
    dumpPages(pages, n);
    check("LCD back to normal pages", pagesContain(pages, n, "STATUS:NORMAL"));
    check("no CRITICAL text left",    !pagesContain(pages, n, "CRITICAL"));
  }

  printf("\n================ RESULT ================\n");
  printf("checks run: %d   failures: %d\n", checks, failures);
  printf("%s\n", failures == 0 ? "ALL HOST LOGIC CHECKS PASSED" : "HOST LOGIC CHECKS FAILED");
  return failures == 0 ? 0 : 1;
}
