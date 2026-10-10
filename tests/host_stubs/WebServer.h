#ifndef WEBSERVER_STUB_H
#define WEBSERVER_STUB_H
#include "Arduino.h"
class WebServer {
public:
  WebServer(int) {}
  void on(const char*, void (*)()) {}
  void begin() {}
  void handleClient() {}
  void send(int, const char*, const String&) {}
};
#endif
