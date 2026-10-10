/*
  logic.h
  Blood Bank Cold Storage Monitoring System
  Academic embedded-systems proof-of-concept only.

  This file holds the pure decision logic (no Arduino calls), so that:
    - sketch.ino includes it and runs it on the ESP32 / Wokwi
    - tests/host_logic_test.cpp includes THE SAME FILE and runs it on a PC

  Plain C-style code on purpose: char buffers + snprintf, no String class,
  no dynamic memory, no templates.
*/

#ifndef LOGIC_H
#define LOGIC_H

#include <stdio.h>
#include <string.h>

/* ---------------- Configuration ---------------- */

/* Safe storage window for this prototype, inclusive. */
#define TEMP_MIN_C 2.0f
#define TEMP_MAX_C 6.0f

/* Door must stay open this long before it becomes a critical alert. */
#define DOOR_ALERT_MS 5000UL

/* Temperature condition codes */
#define TEMP_NORMAL 0
#define TEMP_HIGH   1
#define TEMP_LOW    2

/* ---------------- Inventory ---------------- */

struct BloodUnit {
  const char *code;        /* BG001                */
  const char *group;       /* O+                   */
  const char *expiryText;  /* 20 Aug               */
  int         expiryMonth; /* 8  (used for FEFO)   */
  int         expiryDay;   /* 20 (used for FEFO)   */
  const char *location;    /* A1                   */
  const char *uid;         /* 01:02:03:04          */
};

#define UNIT_COUNT 4

static const struct BloodUnit INVENTORY[UNIT_COUNT] = {
  { "BG001", "O+",  "20 Aug", 8, 20, "A1", "01:02:03:04" },
  { "BG002", "A+",  "16 Aug", 8, 16, "A2", "11:22:33:44" },
  { "BG003", "B+",  "25 Aug", 8, 25, "B1", "55:66:77:88" },
  { "BG004", "AB+", "30 Aug", 8, 30, "B2", "AA:BB:CC:DD" }
};

/* ---------------- Temperature ---------------- */

static int classifyTemperature(float tempC) {
  if (tempC < TEMP_MIN_C) return TEMP_LOW;
  if (tempC > TEMP_MAX_C) return TEMP_HIGH;
  return TEMP_NORMAL;
}

/* ---------------- RFID helpers ---------------- */

/* Turn raw UID bytes into "01:02:03:04" (uppercase hex, colon separated). */
static void formatUid(const unsigned char *bytes, int len, char *out, int outSize) {
  int pos = 0;
  out[0] = '\0';
  for (int i = 0; i < len; i++) {
    if (pos + 3 >= outSize) break;
    if (i > 0) out[pos++] = ':';
    pos += snprintf(out + pos, outSize - pos, "%02X", bytes[i]);
  }
  out[pos] = '\0';
}

/* Make a UID string comparable: uppercase, keep only hex digits and colons. */
static void normalizeUid(const char *in, char *out, int outSize) {
  int pos = 0;
  for (int i = 0; in[i] != '\0' && pos < outSize - 1; i++) {
    char c = in[i];
    if (c >= 'a' && c <= 'f') c = (char)(c - 'a' + 'A');
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || c == ':') {
      out[pos++] = c;
    }
  }
  out[pos] = '\0';
}

/* Returns the inventory index for a UID, or -1 if the card is unknown. */
static int findUnitByUid(const char *uid) {
  char clean[32];
  normalizeUid(uid, clean, sizeof(clean));
  for (int i = 0; i < UNIT_COUNT; i++) {
    if (strcmp(clean, INVENTORY[i].uid) == 0) return i;
  }
  return -1;
}

/* ---------------- FEFO ---------------- */

/*
  Demo reference date.

  The ESP32 in this prototype has no RTC and no NTP time source, so "today" is a
  documented compile-time constant rather than a real clock reading. It is set to
  15 August, which is before every expiry date in the demo inventory, so no unit
  is expired and the demo result stays BG002 / A+ / 16 Aug / A2.

  Moving this date past a unit's expiry excludes that unit from FEFO.
*/
#define DEMO_DATE_MONTH 8
#define DEMO_DATE_DAY   15

/* A unit is expired once the reference date is PAST its expiry date. */
static int isExpiredOn(int index, int month, int day) {
  if (INVENTORY[index].expiryMonth < month) return 1;
  if (INVENTORY[index].expiryMonth == month && INVENTORY[index].expiryDay < day) return 1;
  return 0;
}

/*
  Deterministic First Expiry, First Out.
  Expired units are excluded first; among the remaining valid units the earliest
  (month, day) wins. Ties break to the lower inventory index, so the answer never
  changes between runs. Returns -1 if every unit is expired.
*/
static int fefoIndexOn(int month, int day) {
  int best = -1;
  for (int i = 0; i < UNIT_COUNT; i++) {
    if (isExpiredOn(i, month, day)) continue;
    if (best < 0) { best = i; continue; }
    int betterMonth = INVENTORY[i].expiryMonth < INVENTORY[best].expiryMonth;
    int sameMonth   = INVENTORY[i].expiryMonth == INVENTORY[best].expiryMonth;
    int betterDay   = INVENTORY[i].expiryDay   < INVENTORY[best].expiryDay;
    if (betterMonth || (sameMonth && betterDay)) best = i;
  }
  return best;
}

static int fefoIndex(void) {
  return fefoIndexOn(DEMO_DATE_MONTH, DEMO_DATE_DAY);
}

/* ---------------- System state ---------------- */

struct SystemState {
  float tempC;
  int   humidity;

  int   tempStatus;        /* TEMP_NORMAL / TEMP_HIGH / TEMP_LOW */
  int   doorOpen;          /* 1 = OPEN, 0 = CLOSED               */
  int   doorAlert;         /* 1 = open >= DOOR_ALERT_MS          */
  unsigned long doorOpenSince;

  int   critical;          /* 1 if any critical condition active */

  int   ledRed;
  int   ledGreen;
  int   buzzer;

  char  lastUid[32];
  int   lastUnitIndex;     /* -1 = none scanned / unknown card   */
};

static void systemInit(struct SystemState *s) {
  s->tempC = 0.0f;
  s->humidity = 0;
  s->tempStatus = TEMP_NORMAL;
  s->doorOpen = 0;
  s->doorAlert = 0;
  s->doorOpenSince = 0;
  s->critical = 0;
  s->ledRed = 0;
  s->ledGreen = 1;
  s->buzzer = 0;
  s->lastUid[0] = '\0';
  s->lastUnitIndex = -1;
}

/*
  One non-blocking update step.
  nowMs is millis() on the ESP32, or a simulated clock in the host tests.
  Nothing here waits or delays; the 5 second door rule is a timestamp comparison.
*/
static void systemUpdate(struct SystemState *s, float tempC, int humidity,
                         int doorOpen, unsigned long nowMs) {
  s->tempC = tempC;
  s->humidity = humidity;
  s->tempStatus = classifyTemperature(tempC);

  if (doorOpen) {
    if (!s->doorOpen) {           /* just opened -> start the timer */
      s->doorOpen = 1;
      s->doorOpenSince = nowMs;
    }
    if (!s->doorAlert && (nowMs - s->doorOpenSince) >= DOOR_ALERT_MS) {
      s->doorAlert = 1;
    }
  } else {                        /* closed -> clear timer and alert */
    s->doorOpen = 0;
    s->doorAlert = 0;
    s->doorOpenSince = 0;
  }

  s->critical = (s->tempStatus != TEMP_NORMAL || s->doorAlert) ? 1 : 0;

  /* ACT stage */
  s->ledRed   = s->critical ? 1 : 0;
  s->ledGreen = s->critical ? 0 : 1;
  s->buzzer   = s->critical ? 1 : 0;
}

/* ---------------- Serial event strings ---------------- */

static const char *tempStatusEvent(int tempStatus) {
  if (tempStatus == TEMP_HIGH) return "CRITICAL HIGH TEMP";
  if (tempStatus == TEMP_LOW)  return "CRITICAL LOW TEMP";
  return "TEMP STATUS: NORMAL";
}

static const char *doorStatusEvent(int doorOpen, int doorAlert) {
  if (doorAlert) return "CRITICAL DOOR OPEN";
  if (doorOpen)  return "DOOR OPEN";
  return "DOOR STATUS: CLOSED";
}

static const char *systemStatusEvent(int critical) {
  return critical ? "SYSTEM STATUS: CRITICAL" : "SYSTEM STATUS: NORMAL";
}

/* ---------------- LCD pages (16x2) ---------------- */

struct LcdPage {
  char line1[17];
  char line2[17];
};

#define MAX_LCD_PAGES 6

/* "TEMP:  5.0 C" */
static void makeTempLine(float tempC, char *out, int outSize) {
  snprintf(out, outSize, "TEMP: %5.1f C", (double)tempC);
}

/*
  Builds the page list the LCD should rotate through.

  Priority scheme:
    - If ANY critical condition is active, ONLY critical pages are built,
      and each cause gets two pages that name it explicitly.
      Temperature critical pages come first, then door critical pages,
      so a combined fault shows both causes in rotation.
    - Otherwise the normal information pages are built.

  Returns the number of pages written.
*/
static int buildLcdPages(const struct SystemState *s,
                         const char *fefoCode,
                         struct LcdPage *pages, int maxPages) {
  int n = 0;
  char tempLine[17];
  makeTempLine(s->tempC, tempLine, sizeof(tempLine));

  if (s->critical) {
    if (s->tempStatus != TEMP_NORMAL && n + 2 <= maxPages) {
      const char *cause = (s->tempStatus == TEMP_HIGH) ? "HIGH TEMP" : "LOW TEMP";
      snprintf(pages[n].line1, 17, "%s", tempLine);
      snprintf(pages[n].line2, 17, "CRITICAL:");
      n++;
      snprintf(pages[n].line1, 17, "CRITICAL:");
      snprintf(pages[n].line2, 17, "%s", cause);
      n++;
    }
    if (s->doorAlert && n + 2 <= maxPages) {
      snprintf(pages[n].line1, 17, "DOOR: OPEN");
      snprintf(pages[n].line2, 17, "CRITICAL:");
      n++;
      snprintf(pages[n].line1, 17, "CRITICAL:");
      snprintf(pages[n].line2, 17, "DOOR OPEN");
      n++;
    }
    return n;
  }

  /* Normal pages: temperature first, it is the most important value. */
  if (n < maxPages) {
    snprintf(pages[n].line1, 17, "%s", tempLine);
    snprintf(pages[n].line2, 17, "STATUS:NORMAL");
    n++;
  }
  if (n < maxPages) {
    snprintf(pages[n].line1, 17, "HUM: %3d %%", s->humidity);
    snprintf(pages[n].line2, 17, "DOOR: %s", s->doorOpen ? "OPEN" : "CLOSED");
    n++;
  }
  if (n < maxPages) {
    if (s->lastUnitIndex >= 0) {
      snprintf(pages[n].line1, 17, "RFID: %s %s",
               INVENTORY[s->lastUnitIndex].code,
               INVENTORY[s->lastUnitIndex].group);
    } else if (s->lastUid[0] != '\0') {
      snprintf(pages[n].line1, 17, "RFID: UNKNOWN");
    } else {
      snprintf(pages[n].line1, 17, "RFID: ---");
    }
    snprintf(pages[n].line2, 17, "FEFO: %s", fefoCode);
    n++;
  }
  return n;
}

#endif /* LOGIC_H */
