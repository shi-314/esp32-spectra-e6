#include "MeteogramScreen.h"

#include <algorithm>
#include <vector>

#include "IndoorSensor.h"
#include "battery.h"

// Each quantity owns one ink, so the chart needs no legend: red temperature, blue water, green wind,
// yellow only for the sun. The panel's green is faint, which keeps wind in the background behind
// temperature. Black carries text and structure.
#define TEMPERATURE_COLOR GxEPD_RED
#define FREEZING_COLOR GxEPD_BLUE
#define PRECIPITATION_COLOR GxEPD_BLUE
#define WIND_COLOR GxEPD_GREEN
#define SUN_COLOR GxEPD_YELLOW
#define INK GxEPD_BLACK
#define PAPER GxEPD_WHITE

namespace {
// Single spacing scale, so every gap in the layout is a multiple of the same unit
const int UNIT = 8;
const int MARGIN_X = 20;
const int MARGIN_TOP = 14;
const int MARGIN_BOTTOM = 10;

// Header rows share baselines across every column, relative to the header top
const int CAPTION_BASELINE = 10;
const int VALUE_BASELINE = 34;
const int DETAIL_BASELINE = 56;
const int HEADER_RULE_Y = 68;

const int LEFT_GUTTER = 40;
const int RIGHT_GUTTER = 32;

const int CLOUD_ROW_HEIGHT = 12;
const int CLOUD_GAP = 2 * UNIT;
const int SUN_ICON_WIDTH = 30;
const int DROPLET_WIDTH = 16;
const int TIME_AXIS_HEIGHT = 20;

// Tints, as the fraction of pixels inked by the ordered dither
const float NIGHT_TINT = 0.125f;
const float CLOUD_FULL_TINT = 0.75f;
const float GUST_TINT = 0.25f;

// Precipitation at or above this always gets a visible bar, however small the scale makes it
const float WET_HOUR_MM = 0.1f;

// Rain is auto scaled to a rounded full scale with a floor, so a trace of drizzle stays visually small
const float RAIN_MIN_FULL_SCALE_MM = 2.0f;
const float RAIN_AREA_FRACTION = 0.5f;
const float RAIN_NICE_SCALES[] = {2.0f, 5.0f, 10.0f, 20.0f, 50.0f, 100.0f};

// A calm day should look calm, so the wind scale never shrinks below a moderate breeze
const float WIND_MIN_FULL_SCALE = 6.0f;

const char *const WEEKDAYS[] = {"Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed"};  // 1970-01-01 was a Thursday

// 4x4 Bayer matrix: spreads any fraction of inked pixels as evenly as possible
const uint8_t BAYER4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

bool ditherOn(int x, int y, float fraction) { return (BAYER4[y & 3][x & 3] + 0.5f) / 16.0f < fraction; }

float rainFullScale(float maxPrecipitation) {
  float needed = max(maxPrecipitation, RAIN_MIN_FULL_SCALE_MM);
  for (size_t i = 0; i < sizeof(RAIN_NICE_SCALES) / sizeof(RAIN_NICE_SCALES[0]); i++) {
    if (needed <= RAIN_NICE_SCALES[i]) return RAIN_NICE_SCALES[i];
  }
  return needed;
}

// Smallest round step that divides the range into at most maxIntervals
float niceStep(float range, int maxIntervals) {
  const float steps[] = {1.0f, 2.0f, 5.0f, 10.0f, 20.0f, 50.0f};
  for (float step : steps) {
    if (range / step <= maxIntervals) return step;
  }
  return 100.0f;
}

long daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const long era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra = (unsigned)(year - era * 400);
  const unsigned dayOfYear = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
  const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return era * 146097 + (long)dayOfEra - 719468;
}

// Minutes since 1970 for a local ISO timestamp (YYYY-MM-DDTHH:MM), or -1 when malformed
long isoToMinutes(const String &iso) {
  if (iso.length() < 16 || iso.charAt(10) != 'T') return -1;
  long days = daysFromCivil(iso.substring(0, 4).toInt(), iso.substring(5, 7).toInt(), iso.substring(8, 10).toInt());
  return days * 1440L + iso.substring(11, 13).toInt() * 60 + iso.substring(14, 16).toInt();
}

// Uppercases ASCII and the Latin-1 letters the fonts carry (ä, ö, ü, é, ...), which String::toUpperCase
// leaves alone since they are two bytes in UTF-8
String upperCase(const String &text) {
  String result;
  for (unsigned i = 0; i < text.length(); i++) {
    uint8_t c = text.charAt(i);
    if (c == 0xC3 && i + 1 < text.length()) {
      uint8_t next = text.charAt(i + 1);
      // U+00E0-U+00FE map to U+00C0-U+00DE, except the division sign U+00F7
      if (next >= 0xA0 && next <= 0xBE && next != 0xB7) next -= 0x20;
      result += (char)c;
      result += (char)next;
      i++;
    } else {
      result += (char)toupper(c);
    }
  }
  return result;
}

String clockTime(const String &iso) { return iso.length() >= 16 ? iso.substring(11, 16) : String(""); }

// Temperatures round to whole degrees on the axis but keep a decimal in the header
String formatDegrees(float value, int decimals) {
  String text = String(value, decimals);
  if (text == "-0" || text == "-0.0") text = text.substring(1);
  return text + "°";
}
}  // namespace

MeteogramScreen::MeteogramScreen(DisplayType &display, const WeatherForecast &forecast, const String &locationName)
    : display(display),
      forecast(forecast),
      locationName(locationName),
      heroFont(u8g2_font_fub30_tf),
      valueFont(u8g2_font_helvB18_tf),
      detailFont(u8g2_font_helvB12_tf),
      labelFont(u8g2_font_helvB10_tf),
      captionFont(u8g2_font_helvB08_tf) {
  gfx.begin(display);
}

int MeteogramScreen::timeToX(long minutes) const { return round(plotX + (minutes - windowStart) / 60.0f * xStep); }

float MeteogramScreen::valueToY(float value, const Axis &axis, int top, int height) {
  return top + height - (value - axis.min) / (axis.max - axis.min) * height;
}

// Catmull-Rom through the hourly readings, clamped to the neighbouring samples so the smoothing
// never invents extremes the forecast does not contain
float MeteogramScreen::sampleSeries(const std::vector<float> &values, float position) const {
  int i = constrain((int)floorf(position), 0, pointCount - 2);
  float t = constrain(position - i, 0.0f, 1.0f);

  float p0 = values[max(i - 1, 0)];
  float p1 = values[i];
  float p2 = values[i + 1];
  float p3 = values[min(i + 2, pointCount - 1)];

  float t2 = t * t;
  float t3 = t2 * t;
  float value = 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                        (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
  return constrain(value, min(p1, p2), max(p1, p2));
}

void MeteogramScreen::fillDithered(int x, int y, int w, int h, uint16_t color, float fraction) {
  if (fraction <= 0.0f) return;
  for (int py = y; py < y + h; py++) {
    for (int px = x; px < x + w; px++) {
      if (ditherOn(px, py, fraction)) display.drawPixel(px, py, color);
    }
  }
}

void MeteogramScreen::stampDisc(int x, int y, int radius, uint16_t color) {
  for (int dy = -radius; dy <= radius; dy++) {
    for (int dx = -radius; dx <= radius; dx++) {
      int px = x + dx, py = y + dy;
      if (dx * dx + dy * dy <= radius * radius + radius && px >= clip.left && px <= clip.right && py >= clip.top &&
          py <= clip.bottom) {
        display.drawPixel(px, py, color);
      }
    }
  }
}

void MeteogramScreen::drawSeries(const std::vector<float> &values, const Axis &axis, int top, int height,
                                 int thickness, uint16_t color, uint16_t belowZeroColor, bool dashed) {
  clip = {plotX, top, plotX + plotW, top + height};
  int radius = max(0, thickness / 2);
  const float dashOn = 7.0f;
  const float dashOff = 5.0f;

  // The halo pass runs over the whole curve first, otherwise it would erase the stroke it follows
  for (int pass = 0; pass < 2; pass++) {
    float dashPhase = 0.0f;
    float previousX = plotX;
    float previousY = valueToY(values[0], axis, top, height);

    for (float px = plotX; px <= plotX + plotW; px += 0.5f) {
      float value = sampleSeries(values, (px - plotX) / xStep);
      float y = constrain(valueToY(value, axis, top, height), (float)top, (float)(top + height));

      // Steep stretches are filled in along the segment so the stroke stays unbroken
      float dx = px - previousX, dy = y - previousY;
      int substeps = max(1, (int)ceilf(max(fabsf(dx), fabsf(dy))));
      for (int s = 1; s <= substeps; s++) {
        float sx = previousX + dx * s / substeps;
        float sy = previousY + dy * s / substeps;
        dashPhase += sqrtf(dx * dx + dy * dy) / substeps;
        if (dashed && fmodf(dashPhase, dashOn + dashOff) >= dashOn) continue;

        if (pass == 0) {
          stampDisc(round(sx), round(sy), radius + 1, PAPER);
        } else {
          stampDisc(round(sx), round(sy), radius, value < 0.0f ? belowZeroColor : color);
        }
      }

      previousX = px;
      previousY = y;
    }
  }
}

void MeteogramScreen::fillBetweenSeries(const std::vector<float> &lower, const std::vector<float> &upper,
                                        const Axis &axis, int top, int height, uint16_t color, float fraction) {
  for (int px = plotX; px <= plotX + plotW; px++) {
    float position = (px - plotX) / xStep;
    int yLow = round(valueToY(sampleSeries(lower, position), axis, top, height));
    int yHigh = round(valueToY(sampleSeries(upper, position), axis, top, height));
    for (int py = max(yHigh, top); py <= min(yLow, top + height); py++) {
      if (ditherOn(px, py, fraction)) display.drawPixel(px, py, color);
    }
  }
}

void MeteogramScreen::drawDottedHLine(int x, int y, int w, int spacing, uint16_t color) {
  for (int px = x; px < x + w; px += spacing) {
    display.drawPixel(px, y, color);
  }
}

void MeteogramScreen::drawDottedVLine(int x, int y, int h, int spacing, uint16_t color) {
  for (int py = y; py < y + h; py += spacing) {
    display.drawPixel(x, py, color);
  }
}

void MeteogramScreen::drawText(const String &text, int x, int y, const uint8_t *font, uint16_t color) {
  gfx.setFont(font);
  // Switching fonts resets U8g2 to opaque mode, which would stamp a paper box behind every glyph
  gfx.setFontMode(1);
  gfx.setForegroundColor(color);
  gfx.setCursor(x, y);
  gfx.print(text);
}

int MeteogramScreen::textWidth(const String &text, const uint8_t *font) {
  gfx.setFont(font);
  return gfx.getUTF8Width(text.c_str());
}

bool MeteogramScreen::placeLabel(const String &text, int centerX, int baseline, const uint8_t *font,
                                 uint16_t color) {
  gfx.setFont(font);
  int width = gfx.getUTF8Width(text.c_str());
  int ascent = gfx.getFontAscent();

  int x = constrain(centerX - width / 2, plotX + 1, plotX + plotW - width - 1);
  Box box = {x - 2, baseline - ascent - 2, x + width + 2, baseline + 2};

  for (const Box &other : placedLabels) {
    if (box.left < other.right && box.right > other.left && box.top < other.bottom && box.bottom > other.top) {
      return false;
    }
  }

  display.fillRect(box.left, box.top, box.right - box.left, box.bottom - box.top, PAPER);
  drawText(text, x, baseline, font, color);
  placedLabels.push_back(box);
  return true;
}

void MeteogramScreen::render() {
  Serial.println("Displaying meteogram screen");

  display.init(115200);
  display.setRotation(ApplicationConfig::DISPLAY_ROTATION);
  display.setFullWindow();
  display.fillScreen(PAPER);

  gfx.setFontDirection(0);
  gfx.setBackgroundColor(PAPER);

  int contentWidth = display.width() - 2 * MARGIN_X;
  drawHeader(MARGIN_X, MARGIN_TOP, contentWidth);

  int chartTop = MARGIN_TOP + HEADER_RULE_Y + 2 * UNIT;
  drawMeteogram(MARGIN_X, chartTop, contentWidth, display.height() - MARGIN_BOTTOM - chartTop);

  display.display();
  display.hibernate();
  Serial.println("Display updated");
}

// ---------------------------------------------------------------------------------------------------
// Header: current conditions on the left, the day's key numbers in captioned columns, status on the right

void MeteogramScreen::drawHeader(int x, int y, int w) {
  int nowRight = drawNowColumn(x, y);
  int statusLeft = drawStatusColumn(x + w, y);
  int indoorLeft = statusLeft;

  // Indoor readings sit beside the status on the right, apart from the outdoor conditions on the left
  IndoorReading indoor = readIndoorSensor();
  if (indoor.valid) {
    String caption = "INDOOR";
    String temperature = formatDegrees(indoor.temperature, 1);
    String humidity = String(indoor.humidity, 0) + "%";
    int humidityWidth = DROPLET_WIDTH + UNIT / 2 + textWidth(humidity, detailFont);
    int width = max(max(textWidth(caption, captionFont), textWidth(temperature, valueFont)), humidityWidth);
    int dividerX = statusLeft - 2 * UNIT;
    int indoorX = dividerX - 2 * UNIT - width;

    drawDottedVLine(dividerX, y + 2, DETAIL_BASELINE, 2, INK);
    drawText(caption, indoorX, y + CAPTION_BASELINE, captionFont, INK);
    drawText(temperature, indoorX, y + VALUE_BASELINE, valueFont, INK);
    drawDroplet(indoorX, y + DETAIL_BASELINE);
    drawText(humidity, indoorX + DROPLET_WIDTH + UNIT / 2, y + DETAIL_BASELINE, detailFont, INK);
    indoorLeft = indoorX;
  }

  drawSunTimes(nowRight, indoorLeft, y);
  display.drawFastHLine(x, y + HEADER_RULE_Y, w, INK);
}

// Returns its right edge, so the sun times can be centred in the space that is left
int MeteogramScreen::drawNowColumn(int x, int top) {
  String location = upperCase(locationName);
  String temperature = formatDegrees(forecast.currentTemperature, 1);
  String feels = "Feels like " + formatDegrees(forecast.currentApparentTemperature, 0);

  int textX = x + textWidth(temperature, heroFont) + 2 * UNIT;

  drawText(location, x, top + CAPTION_BASELINE, captionFont, INK);
  drawText(temperature, x, top + DETAIL_BASELINE, heroFont, INK);
  drawText(forecast.currentWeatherDescription, textX, top + VALUE_BASELINE, detailFont, INK);
  drawText(feels, textX, top + DETAIL_BASELINE, detailFont, INK);
  return textX + max(textWidth(forecast.currentWeatherDescription, detailFont), textWidth(feels, detailFont));
}

// Sunrise and sunset side by side, centred between the outdoor conditions and the indoor readings
void MeteogramScreen::drawSunTimes(int left, int right, int top) {
  size_t days = min(forecast.sunrises.size(), forecast.sunsets.size());
  if (days == 0) return;

  // Once today's sun has set, tomorrow's times are the useful ones
  size_t day = 0;
  if (days > 1 && isoToMinutes(forecast.currentTime) > isoToMinutes(forecast.sunsets[0])) day = 1;

  String rise = clockTime(forecast.sunrises[day]);
  String set = clockTime(forecast.sunsets[day]);

  const int iconGap = UNIT - 2;
  const int pairGap = 3 * UNIT;
  int riseWidth = SUN_ICON_WIDTH + iconGap + textWidth(rise, valueFont);
  int width = riseWidth + pairGap + SUN_ICON_WIDTH + iconGap + textWidth(set, valueFont);

  int x = left + (right - left - width) / 2;
  int baseline = top + VALUE_BASELINE;
  drawSunEventIcon(x, baseline, true);
  drawText(rise, x + SUN_ICON_WIDTH + iconGap, baseline, valueFont, INK);
  int setX = x + riseWidth + pairGap;
  drawSunEventIcon(setX, baseline, false);
  drawText(set, setX + SUN_ICON_WIDTH + iconGap, baseline, valueFont, INK);
}

// Returns its left edge, so the indoor readings can be placed against it
int MeteogramScreen::drawStatusColumn(int right, int top) {
  String date = upperCase(forecast.lastUpdateDate);
  String updated = forecast.lastUpdateTime;

  drawText(date, right - textWidth(date, captionFont), top + CAPTION_BASELINE, captionFont, INK);
  drawText(updated, right - textWidth(updated, valueFont), top + VALUE_BASELINE, valueFont, INK);
  int batteryLeft = drawBatteryIndicator(right, top + DETAIL_BASELINE);
  return min(min(right - textWidth(date, captionFont), right - textWidth(updated, valueFont)), batteryLeft);
}

int MeteogramScreen::drawBatteryIndicator(int right, int baseline) {
  const int bodyWidth = 3 * UNIT;
  const int bodyHeight = 12;
  const int capWidth = 3;
  const int capHeight = 6;

  int percentage = constrain(getBatteryPercentage(), 0, 100);
  String label = String(percentage) + "%";
  uint16_t color = percentage <= 20 ? GxEPD_RED : INK;

  int labelWidth = textWidth(label, labelFont);
  int x = right - labelWidth - UNIT / 2 - capWidth - bodyWidth;
  int y = baseline - bodyHeight + 1;

  display.drawRect(x, y, bodyWidth, bodyHeight, color);
  display.fillRect(x + bodyWidth, y + (bodyHeight - capHeight) / 2, capWidth, capHeight, color);

  // Fill proportionally, leaving a one pixel gap to the outline
  int fillWidth = round((bodyWidth - 4) * percentage / 100.0f);
  if (fillWidth > 0) {
    display.fillRect(x + 2, y + 2, fillWidth, bodyHeight - 4, color);
  }

  drawText(label, right - labelWidth, baseline, labelFont, color);
  return x;
}

// Half sun on the horizon with rays to the sides and an arrow above, pointing up at sunrise and down
// at sunset. The disc is yellow with an ink outline, since yellow alone vanishes against the paper.
// Every stroke is the same two pixels wide, and the horizon sits on the text baseline.
void MeteogramScreen::drawSunEventIcon(int x, int baseline, bool rising) {
  const int STROKE = 2;
  const int radius = 7;
  int centerX = x + SUN_ICON_WIDTH / 2;
  int horizonY = baseline - 2;

  // Upper half of the disc only, the rest is below the horizon
  for (int dy = -radius; dy <= 0; dy++) {
    for (int dx = -radius; dx <= radius; dx++) {
      int distance = dx * dx + dy * dy;
      if (distance <= radius * radius + radius) {
        bool edge = distance > (radius - STROKE) * (radius - STROKE) + (radius - STROKE);
        display.drawPixel(centerX + dx, horizonY + dy, edge ? INK : SUN_COLOR);
      }
    }
  }

  // Rays are stamped with a square pen, so diagonals come out as thick as the straight strokes
  const float angles[] = {15.0f, 50.0f, 130.0f, 165.0f};
  for (float angle : angles) {
    float dx = cosf(angle * PI / 180.0f), dy = -sinf(angle * PI / 180.0f);
    for (float distance = radius + 3; distance <= radius + 6; distance += 0.5f) {
      display.fillRect(centerX + round(dx * distance) - STROKE / 2, horizonY + round(dy * distance) - STROKE / 2, STROKE,
                       STROKE, INK);
    }
  }

  display.fillRect(x, horizonY + 1, SUN_ICON_WIDTH, STROKE, INK);

  int arrowTop = horizonY - radius - 12;
  int arrowBottom = horizonY - radius - 3;
  const int headHeight = 5;
  if (rising) {
    display.fillTriangle(centerX - 4, arrowTop + headHeight, centerX + 4, arrowTop + headHeight, centerX, arrowTop, INK);
    display.fillRect(centerX - 1, arrowTop + headHeight, STROKE, arrowBottom - arrowTop - headHeight + 1, INK);
  } else {
    display.fillRect(centerX - 1, arrowTop, STROKE, arrowBottom - arrowTop - headHeight + 1, INK);
    display.fillTriangle(centerX - 4, arrowBottom - headHeight, centerX + 4, arrowBottom - headHeight, centerX, arrowBottom,
                         INK);
  }
}

// Open Iconic droplet in the water ink, standing in for "RH" beside the humidity; sits on the baseline
void MeteogramScreen::drawDroplet(int x, int baseline) {
  const char dropletGlyph = 72;
  gfx.setFont(u8g2_font_open_iconic_thing_2x_t);
  gfx.setFontMode(1);
  gfx.setForegroundColor(PRECIPITATION_COLOR);
  gfx.setCursor(x, baseline + 1);
  gfx.print(dropletGlyph);
}

void MeteogramScreen::drawMessage(const String &message) {
  int width = textWidth(message, valueFont);
  drawText(message, (display.width() - width) / 2, display.height() / 2, valueFont, INK);
}

// ---------------------------------------------------------------------------------------------------
// Meteogram: cloud layers, sunrise/sunset, temperature with rain, and wind, on one shared time axis

void MeteogramScreen::drawMeteogram(int x, int y, int w, int h) {
  const WeatherForecast &f = forecast;
  pointCount = std::min({f.hourlyTemperatures.size(), f.hourlyWindSpeeds.size(), f.hourlyWindGusts.size(),
                         f.hourlyTime.size(), f.hourlyPrecipitation.size(), f.hourlyCloudCover.size()});
  windowStart = pointCount > 0 ? isoToMinutes(f.hourlyTime[0]) : -1;
  if (pointCount < 2 || windowStart < 0) {
    drawMessage("No weather data available.");
    return;
  }

  plotX = x + LEFT_GUTTER;
  plotW = w - LEFT_GUTTER - RIGHT_GUTTER;
  xStep = (float)plotW / (pointCount - 1);
  placedLabels.clear();

  int cloudTop = y;
  int chartTop = cloudTop + CLOUD_ROW_HEIGHT + CLOUD_GAP;
  int chartBottom = y + h - TIME_AXIS_HEIGHT;

  drawNightShading(chartTop, chartBottom - chartTop);
  drawTimeAxis(chartTop, y + h, chartBottom);
  drawCloudCover(cloudTop);
  drawChart(chartTop, chartBottom - chartTop);

  // Now: a hairline through the cloud bar and chart, with a marker above the cloud bar
  int nowX = timeToX(isoToMinutes(f.currentTime));
  if (nowX >= plotX && nowX <= plotX + plotW) {
    for (int py = cloudTop; py <= chartBottom; py++) {
      if (py <= cloudTop + CLOUD_ROW_HEIGHT || py >= chartTop) display.drawPixel(nowX, py, INK);
    }
    display.fillTriangle(nowX - 5, cloudTop - 7, nowX + 5, cloudTop - 7, nowX, cloudTop - 1, INK);
  }
}

void MeteogramScreen::drawNightShading(int top, int height) {
  size_t days = min(forecast.sunrises.size(), forecast.sunsets.size());
  if (days == 0) return;

  // Nights are the gaps between one sunset and the next sunrise; the first and last are open ended
  long windowEnd = windowStart + (pointCount - 1) * 60L;
  long nightStart = windowStart;
  for (size_t day = 0; day <= days; day++) {
    long nightEnd = day < days ? isoToMinutes(forecast.sunrises[day]) : windowEnd;
    int left = max(timeToX(nightStart), plotX);
    int right = min(timeToX(nightEnd), plotX + plotW);
    if (right > left) fillDithered(left, top, right - left, height, INK, NIGHT_TINT);
    if (day < days) nightStart = isoToMinutes(forecast.sunsets[day]);
  }
}

void MeteogramScreen::drawCloudCover(int top) {
  // Each hour owns the cell centred on its timestamp; denser dither means more of the sky covered
  for (int i = 0; i < pointCount; i++) {
    int left = max((int)round(indexToX(i - 0.5f)), plotX);
    int right = min((int)round(indexToX(i + 0.5f)), plotX + plotW);
    fillDithered(left, top, right - left, CLOUD_ROW_HEIGHT, INK, forecast.hourlyCloudCover[i] / 100.0f * CLOUD_FULL_TINT);
  }
  // The outline keeps clear stretches reading as part of the bar instead of gaps in the layout
  display.drawRect(plotX, top, plotW + 1, CLOUD_ROW_HEIGHT + 1, INK);
  drawCloudIcon(plotX - UNIT / 2, top + CLOUD_ROW_HEIGHT / 2);
}

// Open Iconic cloud (glyph 64 of the weather font), knocked back to a checkerboard so it reads as
// grey like the cloud bar instead of a solid black blob
void MeteogramScreen::drawCloudIcon(int right, int centerY) {
  const char cloudGlyph = 64;

  gfx.setFont(u8g2_font_open_iconic_weather_2x_t);
  gfx.setFontMode(1);
  gfx.setForegroundColor(INK);

  int iconWidth = gfx.getUTF8Width(String(cloudGlyph).c_str());
  int iconHeight = gfx.getFontAscent();
  int x = right - iconWidth;
  int baseline = centerY + iconHeight / 2;

  gfx.setCursor(x, baseline);
  gfx.print(cloudGlyph);

  for (int py = baseline - iconHeight; py <= baseline; py++) {
    for (int px = x; px < x + iconWidth; px++) {
      if ((px + py) % 2 != 0) display.drawPixel(px, py, PAPER);
    }
  }
}

MeteogramScreen::Axis MeteogramScreen::temperatureAxis() {
  const std::vector<float> &temperatures = forecast.hourlyTemperatures;
  float lowest = *std::min_element(temperatures.begin(), temperatures.begin() + pointCount);
  float highest = *std::max_element(temperatures.begin(), temperatures.begin() + pointCount);

  // Round ticks with some headroom so the extreme labels fit above and below the curve
  float span = max(highest - lowest, 4.0f);
  float padding = span * 0.2f;
  Axis axis;
  axis.step = niceStep(span + 2 * padding, 5);
  axis.min = floorf((lowest - padding) / axis.step) * axis.step;
  axis.max = ceilf((highest + padding) / axis.step) * axis.step;
  return axis;
}

// Sharing gridlines with the temperature scale fixes the interval count, so the step is the smallest
// that reaches the strongest gust
MeteogramScreen::Axis MeteogramScreen::windAxis(int intervals) {
  float strongest =
      *std::max_element(forecast.hourlyWindGusts.begin(), forecast.hourlyWindGusts.begin() + pointCount);
  strongest = max(strongest,
                  *std::max_element(forecast.hourlyWindSpeeds.begin(), forecast.hourlyWindSpeeds.begin() + pointCount));
  float needed = max(strongest * 1.15f, WIND_MIN_FULL_SCALE);

  // Wind is a magnitude, so the scale starts at calm rather than at the day's minimum
  Axis axis;
  axis.min = 0.0f;
  const float steps[] = {1, 2, 3, 4, 5, 6, 8, 10, 12, 15, 20, 25, 30, 40, 50};
  axis.step = steps[sizeof(steps) / sizeof(steps[0]) - 1];
  for (float step : steps) {
    if (step * intervals >= needed) {
      axis.step = step;
      break;
    }
  }
  axis.max = axis.step * intervals;
  return axis;
}

void MeteogramScreen::drawTicks(const Axis &axis, int top, int height, bool rightSide, bool withDegrees,
                                bool gridlines) {
  gfx.setFont(labelFont);
  int ascent = gfx.getFontAscent();

  for (float tick = axis.min; tick <= axis.max + 0.01f; tick += axis.step) {
    int y = round(valueToY(tick, axis, top, height));
    if (gridlines && tick > axis.min) drawDottedHLine(plotX, y, plotW, 3, INK);
    String label = withDegrees ? formatDegrees(tick, 0) : String(tick, 0);
    int labelY = constrain(y + ascent / 2, top + ascent, top + height);
    int labelX = rightSide ? plotX + plotW + UNIT / 2 : plotX - UNIT / 2 - textWidth(label, labelFont);
    drawText(label, labelX, labelY, labelFont, INK);
  }
}

void MeteogramScreen::drawFreezingLevel(const Axis &axis, int top, int height) {
  if (axis.min >= 0.0f || axis.max <= 0.0f) return;
  int y = round(valueToY(0.0f, axis, top, height));
  for (int px = plotX; px < plotX + plotW; px += 6) display.drawFastHLine(px, y, 3, FREEZING_COLOR);
}

int MeteogramScreen::rainBarHeight(int index, int height) {
  float maxPrecipitation =
      *std::max_element(forecast.hourlyPrecipitation.begin(), forecast.hourlyPrecipitation.begin() + pointCount);
  float amount = forecast.hourlyPrecipitation[index];
  int barHeight = round(min(amount / rainFullScale(maxPrecipitation), 1.0f) * height * RAIN_AREA_FRACTION);
  return barHeight <= 0 && amount >= WET_HOUR_MM ? 1 : barHeight;
}

// Each amount covers the hour before its timestamp, so the bar spans that hour rather than straddling it
void MeteogramScreen::drawRainBars(int top, int height) {
  for (int i = 1; i < pointCount; i++) {
    int barHeight = rainBarHeight(i, height);
    if (barHeight <= 0) continue;
    int left = round(indexToX(i - 1)) + 2;
    int right = round(indexToX(i)) - 2;
    display.fillRect(left, top + height - barHeight, right - left + 1, barHeight, PRECIPITATION_COLOR);
  }
}

// Extremes are labelled beside the curve, so the exact high and low need no reading off the axis
void MeteogramScreen::drawTemperatureExtremes(const Axis &axis, int top, int height) {
  const std::vector<float> &temperatures = forecast.hourlyTemperatures;
  gfx.setFont(labelFont);
  int ascent = gfx.getFontAscent();

  int highIndex = std::max_element(temperatures.begin(), temperatures.begin() + pointCount) - temperatures.begin();
  int lowIndex = std::min_element(temperatures.begin(), temperatures.begin() + pointCount) - temperatures.begin();
  int highY = round(valueToY(temperatures[highIndex], axis, top, height));
  int lowY = round(valueToY(temperatures[lowIndex], axis, top, height));
  placeLabel(formatDegrees(temperatures[highIndex], 0), indexToX(highIndex), highY - 6, labelFont,
             temperatures[highIndex] < 0 ? FREEZING_COLOR : TEMPERATURE_COLOR);
  if (lowIndex != highIndex) {
    placeLabel(formatDegrees(temperatures[lowIndex], 0), indexToX(lowIndex), lowY + ascent + 6, labelFont,
               temperatures[lowIndex] < 0 ? FREEZING_COLOR : TEMPERATURE_COLOR);
  }
}

// Rain amounts above the notable bars, largest first so they win any collision. They go down before
// the temperature curve, and step inside the bar where the curve would run through them.
void MeteogramScreen::drawRainLabels(const Axis &temperature, int top, int height) {
  std::vector<int> wetHours;
  for (int i = 1; i < pointCount; i++) {
    if (forecast.hourlyPrecipitation[i] >= 0.2f) wetHours.push_back(i);
  }
  std::sort(wetHours.begin(), wetHours.end(),
            [&](int a, int b) { return forecast.hourlyPrecipitation[a] > forecast.hourlyPrecipitation[b]; });

  gfx.setFont(captionFont);
  int ascent = gfx.getFontAscent();
  auto curveCrosses = [&](int left, int right, int boxTop, int boxBottom) {
    for (int px = left; px <= right; px++) {
      float y = valueToY(sampleSeries(forecast.hourlyTemperatures, (px - plotX) / xStep), temperature, top, height);
      if (y + 3 >= boxTop && y - 3 <= boxBottom) return true;
    }
    return false;
  };

  for (int i : wetHours) {
    String label = String(forecast.hourlyPrecipitation[i], 1);
    int barHeight = rainBarHeight(i, height);
    int barTop = top + height - barHeight;
    int centerX = indexToX(i - 0.5f);
    int labelWidth = textWidth(label, captionFont);
    int left = centerX - labelWidth / 2, right = centerX + labelWidth / 2;

    if (!curveCrosses(left, right, barTop - 3 - ascent, barTop - 1)) {
      placeLabel(label, centerX, barTop - 3, captionFont, PRECIPITATION_COLOR);
    } else if (barHeight >= ascent + 6 && labelWidth <= xStep - 4 &&
               !curveCrosses(left, right, barTop + 1, barTop + ascent + 3)) {
      drawText(label, left, barTop + ascent + 3, captionFont, PAPER);
    }
  }
}

// The band between mean wind and gusts shows how gusty it is at a glance
void MeteogramScreen::drawWind(const Axis &axis, int top, int height) {
  fillBetweenSeries(forecast.hourlyWindSpeeds, forecast.hourlyWindGusts, axis, top, height, WIND_COLOR, GUST_TINT);
  drawSeries(forecast.hourlyWindGusts, axis, top, height, 1, WIND_COLOR, WIND_COLOR, true);
  drawSeries(forecast.hourlyWindSpeeds, axis, top, height, 3, WIND_COLOR, WIND_COLOR, false);
}

void MeteogramScreen::drawGustLabel(const Axis &axis, int top, int height) {
  int gustIndex = std::max_element(forecast.hourlyWindGusts.begin(), forecast.hourlyWindGusts.begin() + pointCount) -
                  forecast.hourlyWindGusts.begin();
  int gustY = round(valueToY(forecast.hourlyWindGusts[gustIndex], axis, top, height));
  placeLabel(String(forecast.hourlyWindGusts[gustIndex], 0), indexToX(gustIndex), gustY - 5, labelFont, WIND_COLOR);
}

// Temperature on the left scale and wind on the right, sharing gridlines, with rain along the bottom
void MeteogramScreen::drawChart(int top, int height) {
  Axis temperature = temperatureAxis();
  int intervals = round((temperature.max - temperature.min) / temperature.step);
  Axis wind = windAxis(intervals);

  drawTicks(temperature, top, height, false, true, true);
  drawTicks(wind, top, height, true, false, false);
  drawFreezingLevel(temperature, top, height);

  drawWind(wind, top, height);
  drawRainBars(top, height);
  drawTemperatureExtremes(temperature, top, height);
  drawGustLabel(wind, top, height);
  drawRainLabels(temperature, top, height);
  display.drawFastHLine(plotX, top + height, plotW + 1, INK);
  drawSeries(forecast.hourlyTemperatures, temperature, top, height, 3, TEMPERATURE_COLOR, FREEZING_COLOR, false);

  // The wind unit sits above its scale, level with the gap under the cloud bar
  drawText("m/s", plotX + plotW + UNIT / 2, top - UNIT / 2, labelFont, WIND_COLOR);
}

void MeteogramScreen::drawTimeAxis(int chartTop, int baseline, int gridBottom) {
  for (int i = 0; i < pointCount; i++) {
    String time = forecast.hourlyTime[i];
    int hour = time.substring(11, 13).toInt();
    if (hour % 3 != 0) continue;

    int x = round(indexToX(i));
    bool midnight = hour == 0;

    // Day boundaries get a denser line and the weekday instead of 00:00
    if (x > plotX && x < plotX + plotW) {
      drawDottedVLine(x, chartTop, gridBottom - chartTop, midnight ? 2 : 4, INK);
    }
    display.drawFastVLine(x, gridBottom, 4, INK);

    String label = clockTime(time);
    if (midnight) {
      long days = isoToMinutes(time) / 1440L;
      label = WEEKDAYS[((days % 7) + 7) % 7];
    }
    int labelWidth = textWidth(label, labelFont);
    int labelX = constrain(x - labelWidth / 2, plotX - LEFT_GUTTER, plotX + plotW + RIGHT_GUTTER - labelWidth);
    drawText(label, labelX, baseline, labelFont, INK);
  }
}

int MeteogramScreen::nextRefreshInSeconds() { return 1800; }
