#ifndef WIFI_ERROR_SCREEN_H
#define WIFI_ERROR_SCREEN_H

#include <U8g2_for_Adafruit_GFX.h>

#include "DisplayType.h"
#include "Screen.h"

class WifiErrorScreen : public Screen {
 private:
  DisplayType& display;
  U8G2_FOR_ADAFRUIT_GFX gfx;

 public:
  WifiErrorScreen(DisplayType& display);

  void render() override;
  int nextRefreshInSeconds() override;
};

#endif
