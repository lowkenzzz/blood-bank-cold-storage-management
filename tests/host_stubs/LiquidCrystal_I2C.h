#ifndef LCD_STUB_H
#define LCD_STUB_H
#include "Arduino.h"
class LiquidCrystal_I2C {
public:
  LiquidCrystal_I2C(int, int, int) {}
  void init() {}
  void backlight() {}
  void clear() {}
  void setCursor(int, int) {}
  void print(const char*) {}
  void print(const String&) {}
};
#endif
