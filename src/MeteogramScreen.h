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
  struct Axis {
    float min;
    float max;
    float step;
  };

  struct Box {
    int left, top, right, bottom;
  };

  DisplayType& display;
  U8G2_FOR_ADAFRUIT_GFX gfx;
  WeatherForecast forecast;
  String locationName;

  const uint8_t* heroFont;
  const uint8_t* valueFont;
  const uint8_t* detailFont;
  const uint8_t* labelFont;
  const uint8_t* captionFont;

  // Horizontal time scale shared by every row of the chart
  int pointCount = 0;
  int plotX = 0, plotW = 0;
  float xStep = 0;
  long windowStart = 0;

  // Series drawing clips to this rectangle so strokes never bleed over the panel edge
  Box clip = {0, 0, 0, 0};
  // Annotations already placed, so later ones can step aside instead of overprinting
  std::vector<Box> placedLabels;

  int timeToX(long minutes) const;
  float indexToX(float index) const { return plotX + index * xStep; }
  static float valueToY(float value, const Axis& axis, int top, int height);
  float sampleSeries(const std::vector<float>& values, float position) const;

  // The panel has six inks and no grey, so tints are ordered-dithered from them
  void fillDithered(int x, int y, int w, int h, uint16_t color, float fraction);
  void stampDisc(int x, int y, int radius, uint16_t color);
  // Smoothed, evenly thick curve with a paper-colored halo that keeps it legible over bars and shading.
  // Stretches below zero switch to belowZeroColor.
  void drawSeries(const std::vector<float>& values, const Axis& axis, int top, int height, int thickness,
                  uint16_t color, uint16_t belowZeroColor, bool dashed);
  void fillBetweenSeries(const std::vector<float>& lower, const std::vector<float>& upper, const Axis& axis, int top,
                         int height, uint16_t color, float fraction);
  void drawDottedHLine(int x, int y, int w, int spacing, uint16_t color);
  void drawDottedVLine(int x, int y, int h, int spacing, uint16_t color);

  void drawText(const String& text, int x, int y, const uint8_t* font, uint16_t color);
  int textWidth(const String& text, const uint8_t* font);
  // Draws a label on a paper background unless it would collide with one already placed
  bool placeLabel(const String& text, int centerX, int baseline, const uint8_t* font, uint16_t color);

  void drawHeader(int x, int y, int w);
  int drawNowColumn(int x, int top, bool draw);
  int drawWindColumn(int x, int top, bool draw);
  int drawRainColumn(int x, int top, bool draw);
  int drawSunColumn(int x, int top, bool draw);
  int drawIndoorColumn(int x, int top, bool draw);
  int drawStatusColumn(int right, int top, bool draw);
  void drawCaptionedValue(int x, int top, const String& caption, const String& value, const String& unit,
                          const String& detail, bool draw, int* width);
  int drawBatteryIndicator(int right, int baseline);
  void drawSunIcon(int centerX, int centerY, int radius, bool rising);

  void drawMeteogram(int x, int y, int w, int h);
  void drawNightShading(int top, int height);
  void drawCloudLayers(int top);
  void drawSunMarkers(int centerY);
  void drawTemperaturePanel(int top, int height);
  void drawWindPanel(int top, int height);
  void drawTimeAxis(int chartTop, int baseline, int gridBottom);
  void drawMessage(const String& message);

 public:
  MeteogramScreen(DisplayType& display, const WeatherForecast& forecast, const String& locationName);

  void render() override;
  int nextRefreshInSeconds() override;
};

#endif
