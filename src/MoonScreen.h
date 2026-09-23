#ifndef MOON_SCREEN_H
#define MOON_SCREEN_H

#include <SPI.h>
#include <U8g2_for_Adafruit_GFX.h>

#include "ApplicationConfig.h"
#include "DisplayType.h"
#include "Screen.h"

// The Moon in its current phase, centred on a black screen. The images come from NASA's Scientific
// Visualization Studio, prepared by tools/moon/prepare.py and stored on the microSD card.
class MoonScreen : public Screen {
 private:
  DisplayType& display;
  SPIClass& spi;
  float phase;  // Fraction of the lunation: 0 new, 0.5 full, NAN when unknown

  bool drawMoon(const String& path);
  void drawMessage(const String& message);

 public:
  MoonScreen(DisplayType& display, SPIClass& spi, float phase);

  void render() override;
  int nextRefreshInSeconds() override;
};

#endif
