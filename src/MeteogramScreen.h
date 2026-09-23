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
  int drawSunTimes(int x, int top);
  // Returns its left edge, so neighbouring content can be placed against it
  int drawStatusColumn(int right, int top);
  int drawBatteryIndicator(int right, int baseline);
  void drawSunEventIcon(int x, int baseline, bool rising);
  void drawDroplet(int x, int baseline);

  void drawMeteogram(int x, int y, int w, int h);
  void drawNightShading(int top, int height);
  void drawCloudCover(int top);
  void drawCloudIcon(int right, int centerY);
  Axis temperatureAxis();
  // Wind scale with a fixed number of intervals, so it can share the temperature gridlines
  Axis windAxis(int intervals);
  // endUnit, when given, is appended to the lowest and highest tick labels
  void drawTicks(const Axis& axis, int top, int height, bool rightSide, bool withDegrees, bool gridlines,
                 const char* endUnit = nullptr);
  void drawFreezingLevel(const Axis& axis, int top, int height);
  int rainBarHeight(int index, int height);
  void drawRainBars(int top, int height);
  void drawTemperatureExtremes(const Axis& axis, int top, int height);
  void drawRainLabels(const Axis& temperature, int top, int height);
  void drawWind(const Axis& axis, int top, int height);
  void drawGustLabel(const Axis& axis, int top, int height);
  void drawChart(int top, int height);
  void drawTimeAxis(int chartTop, int baseline, int gridBottom);
  void drawMessage(const String& message);

 public:
  MeteogramScreen(DisplayType& display, const WeatherForecast& forecast, const String& locationName);

  void render() override;
  int nextRefreshInSeconds() override;
};

#endif
