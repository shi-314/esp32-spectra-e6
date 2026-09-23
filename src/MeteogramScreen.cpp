#include "MeteogramScreen.h"

#include <algorithm>
#include <vector>

#include "IndoorSensor.h"
#include "battery.h"

// Each quantity owns one ink, so the axes and header need no legend: red temperature, blue water,
// yellow only for the sun. The panel's green is too faint for strokes, so wind is drawn in black and
// green only tints the gust band. Black also carries text and structure.
#define TEMPERATURE_COLOR GxEPD_RED
#define FREEZING_COLOR GxEPD_BLUE
#define PRECIPITATION_COLOR GxEPD_BLUE
#define WIND_COLOR GxEPD_BLACK
#define GUST_BAND_COLOR GxEPD_GREEN
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

const int CLOUD_ROW_HEIGHT = 8;
const int CLOUD_ROW_GAP = 3;
const int CLOUD_ROWS = 3;
const int SUN_STRIP_HEIGHT = 20;
const int PANEL_GAP = 3 * UNIT;
const int TIME_AXIS_HEIGHT = 20;
const float TEMPERATURE_PANEL_SHARE = 0.56f;

// Tints, as the fraction of pixels inked by the ordered dither
const float NIGHT_TINT = 0.125f;
const float CLOUD_FULL_TINT = 0.75f;
const float GUST_TINT = 0.5f;

// Precipitation at or above this counts as wet for the rain summary and bar labels
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

String clockTime(const String &iso) { return iso.length() >= 16 ? iso.substring(11, 16) : String(""); }

String formatDuration(long minutes) {
  String mm = String(minutes % 60);
  if (mm.length() < 2) mm = "0" + mm;
  return String(minutes / 60) + "h " + mm + "m";
}

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
  // Measure first so the middle columns can share the remaining width evenly
  int nowWidth = drawNowColumn(x, y, false);
  int statusWidth = drawStatusColumn(x + w, y, false);
  int columnWidths[] = {drawWindColumn(0, y, false), drawRainColumn(0, y, false), drawSunColumn(0, y, false),
                        drawIndoorColumn(0, y, false)};

  int columnCount = 0;
  int columnsTotal = 0;
  for (int width : columnWidths) {
    if (width > 0) {
      columnCount++;
      columnsTotal += width;
    }
  }

  int gap = (w - nowWidth - statusWidth - columnsTotal) / (columnCount + 1);
  drawNowColumn(x, y, true);
  drawStatusColumn(x + w, y, true);

  int cursor = x + nowWidth + gap;
  int (MeteogramScreen::*columns[])(int, int, bool) = {&MeteogramScreen::drawWindColumn,
                                                       &MeteogramScreen::drawRainColumn,
                                                       &MeteogramScreen::drawSunColumn,
                                                       &MeteogramScreen::drawIndoorColumn};
  for (int i = 0; i < 4; i++) {
    if (columnWidths[i] == 0) continue;
    // Faint divider centred in the gap before each column
    drawDottedVLine(cursor - gap / 2, y + 2, DETAIL_BASELINE, 2, INK);
    (this->*columns[i])(cursor, y, true);
    cursor += columnWidths[i] + gap;
  }
  drawDottedVLine(cursor - gap / 2, y + 2, DETAIL_BASELINE, 2, INK);

  display.drawFastHLine(x, y + HEADER_RULE_Y, w, INK);
}

void MeteogramScreen::drawCaptionedValue(int x, int top, const String &caption, const String &value,
                                         const String &unit, const String &detail, bool draw, int *width) {
  int valueWidth = textWidth(value, valueFont);
  int unitWidth = unit.length() > 0 ? textWidth(unit, labelFont) + 3 : 0;
  *width = max(max(textWidth(caption, captionFont), valueWidth + unitWidth), textWidth(detail, detailFont));
  if (!draw) return;

  drawText(caption, x, top + CAPTION_BASELINE, captionFont, INK);
  drawText(value, x, top + VALUE_BASELINE, valueFont, INK);
  if (unitWidth > 0) drawText(unit, x + valueWidth + 3, top + VALUE_BASELINE, labelFont, INK);
  drawText(detail, x, top + DETAIL_BASELINE, detailFont, INK);
}

int MeteogramScreen::drawNowColumn(int x, int top, bool draw) {
  String location = locationName;
  location.toUpperCase();
  String temperature = formatDegrees(forecast.currentTemperature, 1);
  String feels = "Feels like " + formatDegrees(forecast.currentApparentTemperature, 0);

  int heroWidth = textWidth(temperature, heroFont);
  int textX = x + heroWidth + 2 * UNIT;
  int width = max(textWidth(location, captionFont),
                  heroWidth + 2 * UNIT +
                      max(textWidth(forecast.currentWeatherDescription, detailFont), textWidth(feels, detailFont)));
  if (!draw) return width;

  drawText(location, x, top + CAPTION_BASELINE, captionFont, INK);
  drawText(temperature, x, top + DETAIL_BASELINE, heroFont, INK);
  drawText(forecast.currentWeatherDescription, textX, top + VALUE_BASELINE, detailFont, INK);
  drawText(feels, textX, top + DETAIL_BASELINE, detailFont, INK);
  return width;
}

int MeteogramScreen::drawWindColumn(int x, int top, bool draw) {
  int width;
  drawCaptionedValue(x, top, "WIND", String(forecast.currentWindSpeed, 1), "m/s",
                     "Gusts " + String(forecast.currentWindGusts, 1), draw, &width);
  return width;
}

int MeteogramScreen::drawRainColumn(int x, int top, bool draw) {
  int count = forecast.hourlyPrecipitation.size();
  if (count < 2 || forecast.hourlyTime.size() < (size_t)count) return 0;

  // Each hourly amount is the sum over the hour before its timestamp, so the hour now in progress
  // is the first one whose timestamp lies ahead
  long now = isoToMinutes(forecast.currentTime);
  int current = count - 1;
  for (int i = 0; i < count; i++) {
    if (isoToMinutes(forecast.hourlyTime[i]) > now) {
      current = i;
      break;
    }
  }

  float total = 0.0f;
  for (int i = current; i < count; i++) total += forecast.hourlyPrecipitation[i];

  // One line on when that changes, which is usually the question when heading out
  bool wetNow = forecast.hourlyPrecipitation[current] >= WET_HOUR_MM;
  String detail = wetNow ? "Wet all day" : "Dry all day";
  for (int i = current + 1; i < count; i++) {
    if ((forecast.hourlyPrecipitation[i] >= WET_HOUR_MM) != wetNow) {
      detail = String(wetNow ? "Dry from " : "Wet from ") + clockTime(forecast.hourlyTime[i - 1]);
      break;
    }
  }

  int width;
  drawCaptionedValue(x, top, "PRECIP 24H", String(total, 1), "mm", detail, draw, &width);
  return width;
}

int MeteogramScreen::drawSunColumn(int x, int top, bool draw) {
  size_t days = min(forecast.sunrises.size(), forecast.sunsets.size());
  if (days == 0) return 0;

  // Once today's sun has set, tomorrow's times are the useful ones
  size_t day = 0;
  if (days > 1 && isoToMinutes(forecast.currentTime) > isoToMinutes(forecast.sunsets[0])) day = 1;

  long sunrise = isoToMinutes(forecast.sunrises[day]);
  long sunset = isoToMinutes(forecast.sunsets[day]);
  String caption = "DAYLIGHT " + formatDuration(sunset - sunrise);
  String rise = clockTime(forecast.sunrises[day]);
  String set = clockTime(forecast.sunsets[day]);

  const int iconWidth = 22;
  int width = max(textWidth(caption, captionFont), iconWidth + max(textWidth(rise, detailFont), textWidth(set, detailFont)));
  if (!draw) return width;

  drawText(caption, x, top + CAPTION_BASELINE, captionFont, INK);
  drawSunIcon(x + 6, top + VALUE_BASELINE - 6, 5, true);
  drawText(rise, x + iconWidth, top + VALUE_BASELINE, detailFont, INK);
  drawSunIcon(x + 6, top + DETAIL_BASELINE - 6, 5, false);
  drawText(set, x + iconWidth, top + DETAIL_BASELINE, detailFont, INK);
  return width;
}

int MeteogramScreen::drawIndoorColumn(int x, int top, bool draw) {
  IndoorReading indoor = readIndoorSensor();
  if (!indoor.valid) return 0;

  int width;
  drawCaptionedValue(x, top, "INDOOR", formatDegrees(indoor.temperature, 1), "", String(indoor.humidity, 0) + "% RH", draw,
                     &width);
  return width;
}

int MeteogramScreen::drawStatusColumn(int right, int top, bool draw) {
  String date = forecast.lastUpdateDate;
  date.toUpperCase();
  String updated = forecast.lastUpdateTime;

  int width = max(max(textWidth(date, captionFont), textWidth(updated, valueFont)), 60);
  if (!draw) return width;

  drawText(date, right - textWidth(date, captionFont), top + CAPTION_BASELINE, captionFont, INK);
  drawText(updated, right - textWidth(updated, valueFont), top + VALUE_BASELINE, valueFont, INK);
  drawBatteryIndicator(right, top + DETAIL_BASELINE);
  return width;
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

// Yellow disc with an ink outline (yellow alone vanishes against the paper) and an arrow for the
// direction the sun is moving
void MeteogramScreen::drawSunIcon(int centerX, int centerY, int radius, bool rising) {
  display.fillCircle(centerX, centerY, radius, SUN_COLOR);
  display.drawCircle(centerX, centerY, radius, INK);

  int arrowX = centerX + radius + 5;
  if (rising) {
    display.fillTriangle(arrowX - 3, centerY + 2, arrowX + 3, centerY + 2, arrowX, centerY - 3, INK);
  } else {
    display.fillTriangle(arrowX - 3, centerY - 2, arrowX + 3, centerY - 2, arrowX, centerY + 3, INK);
  }
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
                         f.hourlyTime.size(), f.hourlyPrecipitation.size(), f.hourlyCloudLow.size(),
                         f.hourlyCloudMid.size(), f.hourlyCloudHigh.size()});
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
  int sunStripTop = cloudTop + CLOUD_ROWS * CLOUD_ROW_HEIGHT + (CLOUD_ROWS - 1) * CLOUD_ROW_GAP;
  int temperatureTop = sunStripTop + SUN_STRIP_HEIGHT;
  int available = y + h - TIME_AXIS_HEIGHT - temperatureTop - PANEL_GAP;
  int temperatureHeight = round(available * TEMPERATURE_PANEL_SHARE);
  int windTop = temperatureTop + temperatureHeight + PANEL_GAP;
  int windHeight = available - temperatureHeight;
  int chartBottom = windTop + windHeight;

  drawNightShading(temperatureTop, temperatureHeight);
  drawNightShading(windTop, windHeight);
  drawTimeAxis(temperatureTop, y + h, chartBottom);
  drawCloudLayers(cloudTop);
  drawTemperaturePanel(temperatureTop, temperatureHeight);
  drawWindPanel(windTop, windHeight);

  // Now: a hairline through every row, with a marker above the cloud layers
  int nowX = timeToX(isoToMinutes(f.currentTime));
  if (nowX >= plotX && nowX <= plotX + plotW) {
    for (int py = cloudTop; py <= chartBottom; py++) {
      if (py < sunStripTop || py >= temperatureTop) display.drawPixel(nowX, py, INK);
    }
    display.fillTriangle(nowX - 5, cloudTop - 7, nowX + 5, cloudTop - 7, nowX, cloudTop - 1, INK);
  }

  drawSunMarkers(sunStripTop + SUN_STRIP_HEIGHT / 2);
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

void MeteogramScreen::drawCloudLayers(int top) {
  const std::vector<float> *layers[] = {&forecast.hourlyCloudHigh, &forecast.hourlyCloudMid, &forecast.hourlyCloudLow};
  const char *names[] = {"High", "Mid", "Low"};

  gfx.setFont(captionFont);
  int ascent = gfx.getFontAscent();

  for (int layer = 0; layer < CLOUD_ROWS; layer++) {
    int rowTop = top + layer * (CLOUD_ROW_HEIGHT + CLOUD_ROW_GAP);
    const std::vector<float> &cover = *layers[layer];

    // Each hour owns the cell centred on its timestamp; denser dither means more of the sky covered
    for (int i = 0; i < pointCount; i++) {
      int left = max((int)round(indexToX(i - 0.5f)), plotX);
      int right = min((int)round(indexToX(i + 0.5f)), plotX + plotW);
      fillDithered(left, rowTop, right - left, CLOUD_ROW_HEIGHT, INK, cover[i] / 100.0f * CLOUD_FULL_TINT);
    }
    drawDottedHLine(plotX, rowTop + CLOUD_ROW_HEIGHT, plotW, 2, INK);

    String name = names[layer];
    drawText(name, plotX - UNIT / 2 - textWidth(name, captionFont), rowTop + (CLOUD_ROW_HEIGHT + ascent) / 2,
             captionFont, INK);
  }
}

void MeteogramScreen::drawSunMarkers(int centerY) {
  gfx.setFont(labelFont);
  int ascent = gfx.getFontAscent();
  long windowEnd = windowStart + (pointCount - 1) * 60L;

  for (int kind = 0; kind < 2; kind++) {
    const std::vector<String> &events = kind == 0 ? forecast.sunrises : forecast.sunsets;
    for (const String &event : events) {
      long minutes = isoToMinutes(event);
      if (minutes < windowStart || minutes > windowEnd) continue;

      int x = timeToX(minutes);
      String time = clockTime(event);
      int labelWidth = textWidth(time, labelFont);

      // Tick down to the panel, icon and time beside it, flipped to the left near the right edge
      drawDottedVLine(x, centerY + 4, SUN_STRIP_HEIGHT / 2 - 4, 2, INK);
      drawSunIcon(x, centerY - 1, 4, kind == 0);
      bool flip = x + 13 + labelWidth > plotX + plotW;
      int labelX = flip ? x - 8 - labelWidth : x + 14;
      drawText(time, labelX, centerY - 1 + ascent / 2, labelFont, INK);
    }
  }
}

void MeteogramScreen::drawTemperaturePanel(int top, int height) {
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

  gfx.setFont(labelFont);
  int ascent = gfx.getFontAscent();

  for (float tick = axis.min; tick <= axis.max + 0.01f; tick += axis.step) {
    int y = round(valueToY(tick, axis, top, height));
    if (tick > axis.min) drawDottedHLine(plotX, y, plotW, 3, INK);
    String label = formatDegrees(tick, 0);
    int labelY = constrain(y + ascent / 2, top + ascent, top + height);
    drawText(label, plotX - UNIT / 2 - textWidth(label, labelFont), labelY, labelFont, INK);
  }

  // Freezing level, only when the range reaches it
  if (axis.min < 0.0f && axis.max > 0.0f) {
    int y = round(valueToY(0.0f, axis, top, height));
    for (int px = plotX; px < plotX + plotW; px += 6) display.drawFastHLine(px, y, 3, FREEZING_COLOR);
  }

  // Rain bars sit behind the curve. Each amount covers the hour before its timestamp, so the bar
  // spans that hour rather than straddling it.
  float maxPrecipitation =
      *std::max_element(forecast.hourlyPrecipitation.begin(), forecast.hourlyPrecipitation.begin() + pointCount);
  float rainScale = rainFullScale(maxPrecipitation);
  int rainHeight = round(height * RAIN_AREA_FRACTION);
  for (int i = 1; i < pointCount; i++) {
    float amount = forecast.hourlyPrecipitation[i];
    int barHeight = round(min(amount / rainScale, 1.0f) * rainHeight);
    if (barHeight <= 0 && amount >= WET_HOUR_MM) barHeight = 1;
    if (barHeight <= 0) continue;
    int left = round(indexToX(i - 1)) + 2;
    int right = round(indexToX(i)) - 2;
    display.fillRect(left, top + height - barHeight, right - left + 1, barHeight, PRECIPITATION_COLOR);
  }

  // Extremes are labelled beside the curve, so the exact high and low need no reading off the axis.
  // They are placed first so rain labels step aside for them.
  gfx.setFont(labelFont);
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

  // Rain amounts above the notable bars, largest first so they win any collision. They go down
  // before the curve, so where the two cross the temperature stays whole.
  std::vector<int> wetHours;
  for (int i = 1; i < pointCount; i++) {
    if (forecast.hourlyPrecipitation[i] >= 0.2f) wetHours.push_back(i);
  }
  std::sort(wetHours.begin(), wetHours.end(),
            [&](int a, int b) { return forecast.hourlyPrecipitation[a] > forecast.hourlyPrecipitation[b]; });
  gfx.setFont(captionFont);
  int captionAscent = gfx.getFontAscent();
  auto curveCrosses = [&](int left, int right, int boxTop, int boxBottom) {
    for (int px = left; px <= right; px++) {
      float y = valueToY(sampleSeries(temperatures, (px - plotX) / xStep), axis, top, height);
      if (y + 3 >= boxTop && y - 3 <= boxBottom) return true;
    }
    return false;
  };

  for (int i : wetHours) {
    float amount = forecast.hourlyPrecipitation[i];
    String label = String(amount, 1);
    int barHeight = round(min(amount / rainScale, 1.0f) * rainHeight);
    int barTop = top + height - barHeight;
    int centerX = indexToX(i - 0.5f);
    int labelWidth = textWidth(label, captionFont);
    int left = centerX - labelWidth / 2, right = centerX + labelWidth / 2;

    if (!curveCrosses(left, right, barTop - 3 - captionAscent, barTop - 1)) {
      placeLabel(label, centerX, barTop - 3, captionFont, PRECIPITATION_COLOR);
    } else if (barHeight >= captionAscent + 6 && labelWidth <= xStep - 4 &&
               !curveCrosses(left, right, barTop + 1, barTop + captionAscent + 3)) {
      // The curve runs just above this bar, so the amount moves inside it, knocked out in paper
      drawText(label, left, barTop + captionAscent + 3, captionFont, PAPER);
    }
  }

  display.drawFastHLine(plotX, top + height, plotW + 1, INK);

  drawSeries(temperatures, axis, top, height, 3, TEMPERATURE_COLOR, FREEZING_COLOR, false);

  // The rain unit sits level with the bars it describes; temperature ticks already carry their degree sign
  drawText("mm", plotX + plotW + UNIT / 2, top + height, labelFont, PRECIPITATION_COLOR);
}

void MeteogramScreen::drawWindPanel(int top, int height) {
  float strongest =
      *std::max_element(forecast.hourlyWindGusts.begin(), forecast.hourlyWindGusts.begin() + pointCount);
  strongest = max(strongest,
                  *std::max_element(forecast.hourlyWindSpeeds.begin(), forecast.hourlyWindSpeeds.begin() + pointCount));

  // Wind is a magnitude, so the scale starts at calm rather than at the day's minimum
  Axis axis;
  float needed = max(strongest * 1.15f, WIND_MIN_FULL_SCALE);
  axis.step = niceStep(needed, 3);
  axis.min = 0.0f;
  axis.max = ceilf(needed / axis.step) * axis.step;

  gfx.setFont(labelFont);
  int ascent = gfx.getFontAscent();

  for (float tick = axis.min; tick <= axis.max + 0.01f; tick += axis.step) {
    int y = round(valueToY(tick, axis, top, height));
    if (tick > axis.min) drawDottedHLine(plotX, y, plotW, 3, INK);
    String label = String(tick, 0);
    int labelY = constrain(y + ascent / 2, top + ascent, top + height);
    drawText(label, plotX - UNIT / 2 - textWidth(label, labelFont), labelY, labelFont, INK);
  }

  display.drawFastHLine(plotX, top + height, plotW + 1, INK);

  // The band between mean wind and gusts shows how gusty it is at a glance
  fillBetweenSeries(forecast.hourlyWindSpeeds, forecast.hourlyWindGusts, axis, top, height, GUST_BAND_COLOR,
                    GUST_TINT);
  drawSeries(forecast.hourlyWindGusts, axis, top, height, 2, WIND_COLOR, WIND_COLOR, true);
  drawSeries(forecast.hourlyWindSpeeds, axis, top, height, 3, WIND_COLOR, WIND_COLOR, false);

  int gustIndex =
      std::max_element(forecast.hourlyWindGusts.begin(), forecast.hourlyWindGusts.begin() + pointCount) -
      forecast.hourlyWindGusts.begin();
  int gustY = round(valueToY(forecast.hourlyWindGusts[gustIndex], axis, top, height));
  placeLabel(String(forecast.hourlyWindGusts[gustIndex], 0), indexToX(gustIndex), gustY - 5, labelFont, WIND_COLOR);

  drawText("m/s", plotX + plotW + UNIT / 2, top + (height + ascent) / 2, labelFont, WIND_COLOR);
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
