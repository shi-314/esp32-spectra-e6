#ifndef CONFIGURATION_SCREEN_H
#define CONFIGURATION_SCREEN_H

#include <memory>
#include <U8g2_for_Adafruit_GFX.h>
#include <qrcode.h>

#include "DisplayType.h"
#include "Screen.h"

class ConfigurationScreen : public Screen {
 private:
  DisplayType& display;
  String accessPointName;
  String accessPointPassword;
  U8G2_FOR_ADAFRUIT_GFX gfx;

  void drawQRCode(const String& wifiString, int x, int y, int scale = 3);
  String generateWiFiQRString() const;

 public:
  ConfigurationScreen(DisplayType& display);

  void render() override;
  int nextRefreshInSeconds() override;
};

#endif
