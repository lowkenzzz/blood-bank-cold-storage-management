#ifndef DHTESP_STUB_H
#define DHTESP_STUB_H
/* Stub for the DHTesp library (beegee-tokyo/DHT sensor library for ESPx),
   the Wokwi-recommended DHT library for ESP32. Real API surface: */
struct TempAndHumidity { float temperature; float humidity; };
class DHTesp {
public:
  enum DHT_MODEL_t { DHT11 = 11, DHT22 = 22 };
  void setup(int, DHT_MODEL_t) {}
  TempAndHumidity getTempAndHumidity() { return TempAndHumidity{5.0f, 45.0f}; }
};
#endif
