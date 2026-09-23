#ifndef METEOGRAM_SCREEN_H
#define METEOGRAM_SCREEN_H

#include <U8g2_for_Adafruit_GFX.h>

#include <vector>

#include "ApplicationConfig.h"
#include "DisplayType.h"
#include "OpenMeteoAPI.h"
#include "Screen.h"

class MeteogramScreen : public Screen {
 private:
  DisplayType& display;
  U8G2_FOR_ADAFRUIT_GFX gfx;
  WeatherForecast forecast;
  String locationName;

  const uint8_t* titleFont;
  const uint8_t* primaryFont;
  const uint8_t* secondaryFont;
  const uint8_t* labelFont;

  int parseHHMMtoMinutes(const String& hhmm);

  // The panel has no grey, so tints are dithered from the six available colors
  void fillDitheredRect(int x, int y, int w, int h, uint16_t color, int density);
  // Series drawing clips to this rectangle so strokes never bleed over the plot border
  int clipLeft = 0, clipTop = 0, clipRight = 0, clipBottom = 0;
  bool withinClip(int x, int y) const;
  void stampDisc(int x, int y, int radius, uint16_t color);
  void stampHalo(int x, int y, int radius, uint16_t color);
  // Draws a series as a smoothed, evenly thick curve; dashed and halo are optional styling
  void drawSeries(const std::vector<float>& values, int count, float minValue, float maxValue, int plotX, int plotY,
                  int plotW, int plotH, int thickness, uint16_t color, bool dashed, bool halo);
  void drawDottedLine(int x0, int y0, int x1, int y1, int thickness, uint16_t color);
  void drawDottedHLine(int x, int y, int w, uint16_t color);

  void drawText(const String& text, int x, int y, const uint8_t* font, uint16_t color);
  int textWidth(const String& text, const uint8_t* font);

  void drawHeader(int x, int y, int w);
  // Returns the left edge it occupied, so neighbouring content can be placed against it
  int drawBatteryIndicator(int right, int baseline);
  void drawThermometerIcon(int x, int top, uint16_t color);
  void drawIndoorReadings(int centerX, int baseline);
  void drawCloudIcon(int right, int centerY);
  void drawMeteogram(int x, int y, int w, int h);
  void drawMessage(const String& message);

 public:
  MeteogramScreen(DisplayType& display, const WeatherForecast& forecast, const String& locationName);

  void render() override;
  int nextRefreshInSeconds() override;
};

#endif
