/*
  host_main.cpp

  Provides the stub globals the sketch expects, plus a simulated clock and a
  simulated door pin, then calls setup() once and loop() many times.

  This is a COMPILE AND SMOKE CHECK ONLY. It proves sketch.ino is valid C++ and
  that setup()/loop() run without crashing against the stub API. It does not
  verify the real Arduino libraries, ESP32 peripherals, Wi-Fi or the LCD.
*/

#include "Arduino.h"
#include "Wire.h"
#include "SPI.h"
#include "WiFi.h"

SerialStub Serial;
WireStub   Wire;
SPIStub    SPI;
WiFiStub   WiFi;

static unsigned long g_millis = 0;
int g_doorPinState = LOW;   /* LOW = closed, HIGH = open */

void pinMode(int, int) {}
void digitalWrite(int, int) {}
int  digitalRead(int pin) { return (pin == 33) ? g_doorPinState : LOW; }
unsigned long millis() { return g_millis; }
void delay(unsigned long ms) { g_millis += ms; }

void setup();
void loop();

int main() {
  printf("--- stub run: setup() ---\n");
  setup();

  printf("--- stub run: 4 s with door CLOSED ---\n");
  for (int i = 0; i < 40; i++) { g_millis += 100; loop(); }

  printf("--- stub run: door OPEN for 7 s ---\n");
  g_doorPinState = HIGH;
  for (int i = 0; i < 70; i++) { g_millis += 100; loop(); }

  printf("--- stub run: door CLOSED again for 3 s ---\n");
  g_doorPinState = LOW;
  for (int i = 0; i < 30; i++) { g_millis += 100; loop(); }

  printf("--- stub run finished without crashing ---\n");
  return 0;
}
