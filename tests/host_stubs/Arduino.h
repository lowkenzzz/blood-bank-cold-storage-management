/*
  Minimal Arduino / ESP32 API stubs.

  PURPOSE: let a normal g++ compile sketch.ino so that syntax errors, typos,
  wrong argument counts and undeclared identifiers are caught here instead of
  being discovered later in Wokwi.

  THIS IS NOT AN ESP32 BUILD. It does not verify library behaviour, timing,
  memory use or peripheral drivers. Only Wokwi or real hardware can do that.
*/

#ifndef ARDUINO_STUB_H
#define ARDUINO_STUB_H

#include <string>
#include <cstdio>
#include <cstdint>

typedef unsigned char byte;

#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0
#define INPUT_PULLUP 2
#define DEC 10

class String {
public:
  std::string s;
  String() {}
  String(const char *v) : s(v) {}
  String(int v) { s = std::to_string(v); }
  String(unsigned int v) { s = std::to_string(v); }
  String(long v) { s = std::to_string(v); }
  String(float v, int digits) { char b[32]; snprintf(b, sizeof(b), "%.*f", digits, v); s = b; }
  String(double v, int digits) { char b[32]; snprintf(b, sizeof(b), "%.*f", digits, v); s = b; }
  String &operator+=(const String &o) { s += o.s; return *this; }
  String &operator+=(const char *o) { s += o; return *this; }
  String operator+(const String &o) const { String r; r.s = s + o.s; return r; }
  String operator+(const char *o) const { String r; r.s = s + o; return r; }
  const char *c_str() const { return s.c_str(); }
};

inline String operator+(const char *a, const String &b) { String r; r.s = std::string(a) + b.s; return r; }

class SerialStub {
public:
  void begin(long) {}
  void print(const char *v) { printf("%s", v); }
  void print(const String &v) { printf("%s", v.c_str()); }
  void print(int v) { printf("%d", v); }
  void print(float v, int d) { printf("%.*f", d, v); }
  void println(const char *v) { printf("%s\n", v); }
  void println(const String &v) { printf("%s\n", v.c_str()); }
  void println(int v) { printf("%d\n", v); }
  void println(float v, int d) { printf("%.*f\n", d, v); }
  void println() { printf("\n"); }
};
extern SerialStub Serial;

void pinMode(int pin, int mode);
void digitalWrite(int pin, int value);
int  digitalRead(int pin);
unsigned long millis();
void delay(unsigned long ms);

#endif
