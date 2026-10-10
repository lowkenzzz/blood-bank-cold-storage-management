#ifndef WIFI_STUB_H
#define WIFI_STUB_H
#include "Arduino.h"
#define WL_CONNECTED 3
#define WIFI_STA 1
class IPAddressStub { public: const char* c_str() const { return "10.0.0.2"; } };
inline SerialStub& operator<<(SerialStub& s, const IPAddressStub&) { return s; }
class WiFiStub {
public:
  void mode(int) {}
  void begin(const char*, const char*) {}
  int status() { return WL_CONNECTED; }
  String localIP() { return String("10.0.0.2"); }
};
extern WiFiStub WiFi;
#endif
