#pragma once
#include "Arduino.h"
#define HTTP_CODE_OK 200
class HTTPClient {
 public:
  void begin(const String&) {}
  int GET() { return -1; }
  String getString() { return String(); }
  void end() {}
};
