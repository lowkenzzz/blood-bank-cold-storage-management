#ifndef DALLAS_STUB_H
#define DALLAS_STUB_H
#include "OneWire.h"
class DallasTemperature {
public:
  DallasTemperature(OneWire*) {}
  void begin() {}
  void requestTemperatures() {}
  float getTempCByIndex(int) { return 5.0f; }
};
#endif
