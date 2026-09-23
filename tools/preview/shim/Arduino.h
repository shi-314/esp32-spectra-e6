// Minimal Arduino shim so the screen code builds and renders on a desktop
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <type_traits>

#ifndef ARDUINO
#define ARDUINO 100
#endif
#define PROGMEM
#define F(s) (s)
#define pgm_read_byte(addr) (*(const unsigned char*)(addr))
class __FlashStringHelper;
typedef bool boolean;

#define PI 3.1415926535897932384626433832795
#define radians(deg) ((deg) * M_PI / 180.0)
#define degrees(rad) ((rad) * 180.0 / M_PI)
using std::max;
using std::min;
template <class T, class L, class H>
typename std::common_type<T, L, H>::type constrain(T x, L lo, H hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}

class String {
 public:
  std::string s;
  String() {}
  String(const char* c) : s(c ? c : "") {}
  String(const std::string& c) : s(c) {}
  explicit String(char c) : s(1, c) {}
  String(int v) : s(std::to_string(v)) {}
  String(unsigned v) : s(std::to_string(v)) {}
  String(long v) : s(std::to_string(v)) {}
  String(unsigned long v) : s(std::to_string(v)) {}
  String(double v, unsigned decimals = 2) {
    char b[64];
    snprintf(b, sizeof(b), "%.*f", decimals, v);
    s = b;
  }
  String(float v, unsigned decimals = 2) : String((double)v, decimals) {}
  unsigned length() const { return s.size(); }
  const char* c_str() const { return s.c_str(); }
  char charAt(unsigned i) const { return i < s.size() ? s[i] : 0; }
  char operator[](unsigned i) const { return charAt(i); }
  String substring(unsigned a) const { return a < s.size() ? String(s.substr(a)) : String(); }
  String substring(unsigned a, unsigned b) const {
    if (a > b) std::swap(a, b);
    if (a >= s.size()) return String();
    return String(s.substr(a, std::min<size_t>(b, s.size()) - a));
  }
  long toInt() const { return atol(s.c_str()); }
  float toFloat() const { return atof(s.c_str()); }
  int indexOf(char c) const { auto p = s.find(c); return p == std::string::npos ? -1 : (int)p; }
  int indexOf(const String& t) const { auto p = s.find(t.s); return p == std::string::npos ? -1 : (int)p; }
  bool startsWith(const String& t) const { return s.rfind(t.s, 0) == 0; }
  bool isEmpty() const { return s.empty(); }
  void reserve(size_t n) { s.reserve(n); }
  bool concat(const char* c) { s += c; return true; }
  bool concat(const char* c, size_t n) { s.append(c, n); return true; }
  bool concat(char c) { s += c; return true; }
  String& operator+=(const String& o) { s += o.s; return *this; }
  String& operator+=(const char* o) { s += o; return *this; }
  String& operator+=(char o) { s += o; return *this; }
  bool operator==(const String& o) const { return s == o.s; }
  bool operator==(const char* o) const { return s == o; }
  bool operator!=(const String& o) const { return s != o.s; }
  void toUpperCase() { for (auto& c : s) c = toupper(c); }
  void trim() {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    s = a == std::string::npos ? "" : s.substr(a, b - a + 1);
  }
};
inline String operator+(const String& a, const String& b) { return String(a.s + b.s); }
inline String operator+(const String& a, const char* b) { return String(a.s + b); }
inline String operator+(const char* a, const String& b) { return String(std::string(a) + b.s); }
inline String operator+(const String& a, char b) { return String(a.s + b); }

class Print {
 public:
  virtual ~Print() {}
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* b, size_t n) {
    size_t r = 0;
    while (n--) r += write(*b++);
    return r;
  }
  size_t write(const char* str) { return write((const uint8_t*)str, strlen(str)); }
  size_t print(const char* str) { return write(str); }
  size_t print(const String& str) { return write(str.c_str()); }
  size_t print(char c) { return write((uint8_t)c); }
  size_t print(int v) { return print(String(v)); }
  size_t print(float v, int d = 2) { return print(String(v, d)); }
  size_t println(const char* str = "") { return print(str) + write((uint8_t)'\n'); }
  size_t println(const String& str) { return print(str) + write((uint8_t)'\n'); }
  size_t printf(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
};

class SerialShim : public Print {
 public:
  size_t write(uint8_t c) override { return fputc(c, stderr) != EOF; }
  void begin(unsigned long) {}
};
extern SerialShim Serial;

inline unsigned long millis() { return 0; }
inline void delay(unsigned long) {}
