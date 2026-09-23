// Stand-in for the GxEPD2 display: an 800x480 framebuffer of the six Spectra 6 inks
#pragma once
#include <Adafruit_GFX.h>

#define GxEPD_BLACK 0x0000
#define GxEPD_WHITE 0xFFFF
#define GxEPD_DARKGREY 0x7BEF
#define GxEPD_LIGHTGREY 0xC618
#define GxEPD_RED 0xF800
#define GxEPD_YELLOW 0xFFE0
#define GxEPD_BLUE 0x001F
#define GxEPD_GREEN 0x07E0
#define GxEPD_ORANGE 0xFC00

class DisplayType : public Adafruit_GFX {
 public:
  static const int W = 800, H = 480;
  uint16_t pixels[W * H];
  DisplayType() : Adafruit_GFX(W, H) { fillScreen(GxEPD_WHITE); }
  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    pixels[y * W + x] = color;
  }
  void init(unsigned long) {}
  void setFullWindow() {}
  void display() {}
  void hibernate() {}
  bool writePPM(const char* path, bool panelColors) const;
};
