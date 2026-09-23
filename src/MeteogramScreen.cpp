#include "MeteogramScreen.h"

#include <algorithm>
#include <vector>

#include "IndoorSensor.h"
#include "battery.h"

// Series colors. Red is kept as the only warm accent and reserved for temperature, the rest of the
// chart stays in neutral greys and cool tones so the six color palette does not fight itself.
#define TEMPERATURE_COLOR GxEPD_RED
#define WIND_COLOR GxEPD_GREEN
#define GUSTS_COLOR GxEPD_GREEN
#define PRECIPITATION_COLOR GxEPD_BLUE
#define CLOUD_COLOR GxEPD_BLACK
#define NOW_COLOR GxEPD_BLACK

// Dither densities used to fake tints of a color (0 = none, 4 = solid)
#define DENSITY_NONE 0
#define DENSITY_LIGHT 1
#define DENSITY_MEDIUM 2
#define DENSITY_HEAVY 3
#define DENSITY_SOLID 4

namespace {
// Single spacing scale, so every gap in the layout is a multiple of the same unit
const int UNIT = 8;
const int MARGIN = 4 * UNIT;

// Header: title baseline, meta baseline, then the rule that closes it
const int HEADER_TITLE_BASELINE = 5 * UNIT;
const int HEADER_META_BASELINE = 8 * UNIT;
const int HEADER_RULE_Y = 10 * UNIT;

const int CLOUD_BAND_HEIGHT = 2 * UNIT;
const int CLOUD_BAND_SPACING = 2 * UNIT;

// Fixed gutters keep the plot centered regardless of how wide the axis labels are
const int AXIS_GUTTER = 7 * UNIT;
const int HOUR_LABEL_HEIGHT = 3 * UNIT;

// Rain is auto scaled like common meteograms, but to a rounded scale with a floor so that a trace of
// drizzle stays visually small, and the chosen scale is drawn as a labelled reference line.
const float RAIN_MIN_FULL_SCALE_MM = 1.0f;
const float RAIN_AREA_FRACTION = 0.55f;
const float RAIN_NICE_SCALES[] = {1.0f, 2.0f, 5.0f, 10.0f, 20.0f, 50.0f, 100.0f};

float rainFullScale(float maxPrecipitation) {
  float needed = max(maxPrecipitation, RAIN_MIN_FULL_SCALE_MM);
  for (size_t i = 0; i < sizeof(RAIN_NICE_SCALES) / sizeof(RAIN_NICE_SCALES[0]); i++) {
    if (needed <= RAIN_NICE_SCALES[i]) return RAIN_NICE_SCALES[i];
  }
  return needed;
}
}  // namespace

MeteogramScreen::MeteogramScreen(DisplayType &display, const WeatherForecast &forecast, const String &locationName)
    : display(display),
      forecast(forecast),
      locationName(locationName),
      titleFont(u8g2_font_helvB24_tf),
      primaryFont(u8g2_font_helvB14_tf),
      secondaryFont(u8g2_font_helvB12_tf),
      labelFont(u8g2_font_helvB10_tf) {
  gfx.begin(display);
}

int MeteogramScreen::parseHHMMtoMinutes(const String &hhmm) {
  if (hhmm.length() != 5 || hhmm.charAt(2) != ':') {
    return -1;
  }
  int hours = hhmm.substring(0, 2).toInt();
  int minutes = hhmm.substring(3, 5).toInt();
  if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59) {
    return -1;
  }
  return hours * 60 + minutes;
}

void MeteogramScreen::fillDitheredRect(int x, int y, int w, int h, uint16_t color, int density) {
  if (density <= DENSITY_NONE) return;

  for (int py = y; py < y + h; py++) {
    for (int px = x; px < x + w; px++) {
      bool on;
      switch (density) {
        case DENSITY_LIGHT:
          on = (px % 2 == 0) && (py % 2 == 0);
          break;
        case DENSITY_MEDIUM:
          on = (px + py) % 2 == 0;
          break;
        case DENSITY_HEAVY:
          on = (px % 2 != 0) || (py % 2 != 0);
          break;
        default:
          on = true;
          break;
      }

      if (on) display.drawPixel(px, py, color);
    }
  }
}

bool MeteogramScreen::withinClip(int x, int y) const {
  return x >= clipLeft && x <= clipRight && y >= clipTop && y <= clipBottom;
}

void MeteogramScreen::stampDisc(int x, int y, int radius, uint16_t color) {
  for (int dy = -radius; dy <= radius; dy++) {
    for (int dx = -radius; dx <= radius; dx++) {
      if (dx * dx + dy * dy <= radius * radius + radius && withinClip(x + dx, y + dy)) {
        display.drawPixel(x + dx, y + dy, color);
      }
    }
  }
}

// A dithered ring just outside the stroke. The panel cannot anti alias, so a half density edge is
// the closest we get to softening the staircase without blurring the curve away.
void MeteogramScreen::stampHalo(int x, int y, int radius, uint16_t color) {
  int outer = radius + 1;
  for (int dy = -outer; dy <= outer; dy++) {
    for (int dx = -outer; dx <= outer; dx++) {
      int distance = dx * dx + dy * dy;
      bool insideCore = distance <= radius * radius + radius;
      bool insideHalo = distance <= outer * outer + outer;
      if (!insideCore && insideHalo && (x + dx + y + dy) % 2 == 0 && withinClip(x + dx, y + dy)) {
        display.drawPixel(x + dx, y + dy, color);
      }
    }
  }
}

void MeteogramScreen::drawSeries(const std::vector<float> &values, int count, float minValue, float maxValue, int plotX,
                                 int plotY, int plotW, int plotH, int thickness, uint16_t color, bool dashed,
                                 bool halo) {
  if (count < 2 || maxValue <= minValue) return;

  clipLeft = plotX + 1;
  clipTop = plotY + 1;
  clipRight = plotX + plotW - 1;
  clipBottom = plotY + plotH - 1;

  float xStep = (float)plotW / (count - 1);
  int radius = max(0, thickness / 2);

  auto valueToY = [&](float value) {
    float normalized = (value - minValue) / (maxValue - minValue);
    return plotY + plotH - normalized * plotH;
  };

  // Centripetal-style Catmull-Rom keeps the curve through every hourly reading while smoothing the
  // corners; clamping to the neighbouring samples stops it overshooting into invented extremes.
  float dashPhase = 0.0f;
  float previousX = plotX;
  float previousY = valueToY(values[0]);

  for (int i = 0; i < count - 1; i++) {
    float p0 = values[max(i - 1, 0)];
    float p1 = values[i];
    float p2 = values[i + 1];
    float p3 = values[min(i + 2, count - 1)];

    int steps = max(2, (int)ceil(xStep));
    for (int step = 1; step <= steps; step++) {
      float t = (float)step / steps;
      float t2 = t * t;
      float t3 = t2 * t;

      float value = 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                            (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
      value = constrain(value, min(p1, p2), max(p1, p2));

      float x = plotX + (i + t) * xStep;
      float y = constrain(valueToY(value), (float)plotY, (float)(plotY + plotH));

      float segment = sqrtf((x - previousX) * (x - previousX) + (y - previousY) * (y - previousY));
      dashPhase += segment;

      bool visible = true;
      if (dashed) {
        const float dashOn = 7.0f;
        const float dashOff = 6.0f;
        visible = fmodf(dashPhase, dashOn + dashOff) < dashOn;
      }

      if (visible) {
        if (halo) stampHalo(round(x), round(y), radius, color);
        stampDisc(round(x), round(y), radius, color);
      }

      previousX = x;
      previousY = y;
    }
  }
}

void MeteogramScreen::drawDottedLine(int x0, int y0, int x1, int y1, int thickness, uint16_t color) {
  int dx = abs(x1 - x0);
  int dy = abs(y1 - y0);
  int sx = x0 < x1 ? 1 : -1;
  int sy = y0 < y1 ? 1 : -1;
  int err = dx - dy;

  int x = x0;
  int y = y0;
  int dotCount = 0;
  const int dotLength = 4;
  const int gapLength = 4;
  bool mostlyHorizontal = dx >= dy;

  while (true) {
    if (dotCount < dotLength) {
      for (int offset = 0; offset < thickness; offset++) {
        int shift = offset - thickness / 2;
        if (mostlyHorizontal) {
          display.drawPixel(x, y + shift, color);
        } else {
          display.drawPixel(x + shift, y, color);
        }
      }
    }

    dotCount++;
    if (dotCount >= dotLength + gapLength) {
      dotCount = 0;
    }

    if (x == x1 && y == y1) break;

    int e2 = 2 * err;
    if (e2 > -dy) {
      err -= dy;
      x += sx;
    }
    if (e2 < dx) {
      err += dx;
      y += sy;
    }
  }
}

void MeteogramScreen::drawDottedHLine(int x, int y, int w, uint16_t color) {
  for (int px = x; px < x + w; px += 3) {
    display.drawPixel(px, y, color);
  }
}

void MeteogramScreen::drawText(const String &text, int x, int y, const uint8_t *font, uint16_t color) {
  gfx.setFont(font);
  gfx.setForegroundColor(color);
  gfx.setCursor(x, y);
  gfx.print(text);
}

int MeteogramScreen::textWidth(const String &text, const uint8_t *font) {
  gfx.setFont(font);
  return gfx.getUTF8Width(text.c_str());
}

void MeteogramScreen::render() {
  Serial.println("Displaying meteogram screen");

  display.init(115200);
  display.setRotation(ApplicationConfig::DISPLAY_ROTATION);
  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);

  gfx.setFontMode(1);
  gfx.setFontDirection(0);
  gfx.setBackgroundColor(GxEPD_WHITE);

  int contentWidth = display.width() - 2 * MARGIN;
  int footerBaseline = display.height() - MARGIN;

  drawHeader(MARGIN, MARGIN, contentWidth);

  int chartY = MARGIN + HEADER_RULE_Y + 2 * UNIT;
  int chartH = footerBaseline - chartY;
  drawMeteogram(MARGIN, chartY, contentWidth, chartH);

  display.display();
  display.hibernate();
  Serial.println("Display updated");
}

void MeteogramScreen::drawHeader(int x, int y, int w) {
  int titleBaseline = y + HEADER_TITLE_BASELINE;
  int metaBaseline = y + HEADER_META_BASELINE;

  String temperature = String(forecast.currentTemperature, 1) + "°";
  int temperatureWidth = textWidth(temperature, titleFont);
  drawText(temperature, x, titleBaseline, titleFont, GxEPD_BLACK);
  drawText(forecast.currentWeatherDescription, x + temperatureWidth + 2 * UNIT, titleBaseline, primaryFont,
           GxEPD_BLACK);

  drawIndoorReadings(x + w / 2, titleBaseline);

  if (locationName.length() > 0) {
    drawText(locationName, x + w - textWidth(locationName, primaryFont), titleBaseline, primaryFont, GxEPD_BLACK);
  }

  int batteryLeft = drawBatteryIndicator(x + w, metaBaseline);

  String meta = forecast.lastUpdateDate;
  if (forecast.lastUpdateTime.length() > 0) {
    meta += (meta.length() > 0 ? "  ·  " : "") + forecast.lastUpdateTime;
  }
  if (meta.length() > 0) {
    drawText(meta, batteryLeft - 2 * UNIT - textWidth(meta, secondaryFont), metaBaseline, secondaryFont, GxEPD_BLACK);
  }

  // Hairline rule closing the header, dithered so it reads as light grey
  fillDitheredRect(x, y + HEADER_RULE_Y, w, 1, GxEPD_BLACK, DENSITY_MEDIUM);
}

// Small cloud drawn from primitives so it stays crisp at this size, then knocked back to a
// checkerboard so it reads as grey like the cloud band rather than a heavy black blob
// Open Iconic cloud (glyph 64 of the weather font), knocked back to a checkerboard so it reads as
// grey like the cloud band instead of a solid black blob
void MeteogramScreen::drawCloudIcon(int right, int centerY) {
  const char cloudGlyph = 64;

  gfx.setFont(u8g2_font_open_iconic_weather_2x_t);
  gfx.setForegroundColor(GxEPD_BLACK);

  int iconWidth = gfx.getUTF8Width(String(cloudGlyph).c_str());
  int iconHeight = gfx.getFontAscent();
  int x = right - iconWidth;
  int baseline = centerY + iconHeight / 2;

  gfx.setCursor(x, baseline);
  gfx.print(cloudGlyph);

  for (int py = baseline - iconHeight; py <= baseline; py++) {
    for (int px = x; px < x + iconWidth; px++) {
      if ((px + py) % 2 != 0) display.drawPixel(px, py, GxEPD_WHITE);
    }
  }
}

int MeteogramScreen::drawBatteryIndicator(int right, int baseline) {
  const int bodyWidth = 3 * UNIT;
  const int bodyHeight = 14;
  const int capWidth = 3;
  const int capHeight = 6;

  int percentage = constrain(getBatteryPercentage(), 0, 100);
  String label = String(percentage) + "%";
  uint16_t color = percentage <= 20 ? TEMPERATURE_COLOR : GxEPD_BLACK;

  int labelWidth = textWidth(label, labelFont);
  int x = right - labelWidth - UNIT - capWidth - bodyWidth;
  int y = baseline - bodyHeight + 3;

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

// Open Iconic has no thermometer, so this one is drawn to match the battery icon's weight
void MeteogramScreen::drawThermometerIcon(int x, int top, uint16_t color) {
  const int stemWidth = 5;
  const int stemHeight = 10;
  int centerX = x + stemWidth / 2 + 1;

  display.fillRect(centerX - stemWidth / 2, top, stemWidth, stemHeight, color);
  display.fillCircle(centerX, top + stemHeight + 3, 5, color);

  // Two ticks knocked out of the stem so it reads as a thermometer rather than a pin
  display.drawFastHLine(centerX, top + 3, 3, GxEPD_WHITE);
  display.drawFastHLine(centerX, top + 6, 3, GxEPD_WHITE);
}

void MeteogramScreen::drawIndoorReadings(int centerX, int baseline) {
  IndoorReading indoor = readIndoorSensor();
  if (!indoor.valid) return;

  const char dropletGlyph = 72;  // Open Iconic "droplet"
  String temperature = String(indoor.temperature, 1) + "°";
  String humidity = String(indoor.humidity, 0) + "%";

  const int thermometerWidth = 12;
  gfx.setFont(u8g2_font_open_iconic_thing_2x_t);
  int dropletWidth = gfx.getUTF8Width(String(dropletGlyph).c_str());
  int dropletAscent = gfx.getFontAscent();

  int temperatureWidth = textWidth(temperature, secondaryFont);
  int humidityWidth = textWidth(humidity, secondaryFont);
  int total = thermometerWidth + UNIT / 2 + temperatureWidth + 3 * UNIT + dropletWidth + UNIT / 2 + humidityWidth;

  int cursorX = centerX - total / 2;
  drawThermometerIcon(cursorX, baseline - 17, GxEPD_BLACK);
  cursorX += thermometerWidth + UNIT / 2;
  drawText(temperature, cursorX, baseline, secondaryFont, GxEPD_BLACK);
  cursorX += temperatureWidth + 3 * UNIT;

  gfx.setFont(u8g2_font_open_iconic_thing_2x_t);
  gfx.setForegroundColor(GxEPD_BLACK);
  gfx.setCursor(cursorX, baseline - (dropletAscent > 16 ? 1 : 0));
  gfx.print(dropletGlyph);
  cursorX += dropletWidth + UNIT / 2;
  drawText(humidity, cursorX, baseline, secondaryFont, GxEPD_BLACK);
}

void MeteogramScreen::drawMessage(const String &message) {
  int width = textWidth(message, primaryFont);
  drawText(message, (display.width() - width) / 2, display.height() / 2, primaryFont, GxEPD_BLACK);
}

void MeteogramScreen::drawMeteogram(int x_base, int y_base, int w, int h) {
  if (forecast.hourlyTemperatures.empty() || forecast.hourlyWindSpeeds.empty() || forecast.hourlyWindGusts.empty() ||
      forecast.hourlyTime.empty() || forecast.hourlyPrecipitation.empty() || forecast.hourlyCloudCoverage.empty()) {
    drawMessage("No weather data available.");
    return;
  }

  int num_points = std::min({(int)forecast.hourlyTemperatures.size(), (int)forecast.hourlyWindSpeeds.size(),
                             (int)forecast.hourlyWindGusts.size(), (int)forecast.hourlyTime.size(),
                             (int)forecast.hourlyPrecipitation.size(), (int)forecast.hourlyCloudCoverage.size(), 24});
  if (num_points <= 1) {
    drawMessage("Not enough weather data.");
    return;
  }

  float min_temp =
      *std::min_element(forecast.hourlyTemperatures.begin(), forecast.hourlyTemperatures.begin() + num_points);
  float max_temp =
      *std::max_element(forecast.hourlyTemperatures.begin(), forecast.hourlyTemperatures.begin() + num_points);

  std::vector<float> allWindData;
  allWindData.insert(allWindData.end(), forecast.hourlyWindSpeeds.begin(),
                     forecast.hourlyWindSpeeds.begin() + num_points);
  allWindData.insert(allWindData.end(), forecast.hourlyWindGusts.begin(),
                     forecast.hourlyWindGusts.begin() + num_points);
  float min_wind = *std::min_element(allWindData.begin(), allWindData.end());
  float max_wind = *std::max_element(allWindData.begin(), allWindData.end());

  float max_precipitation =
      *std::max_element(forecast.hourlyPrecipitation.begin(), forecast.hourlyPrecipitation.begin() + num_points);

  if (max_temp == min_temp) max_temp += 1.0f;
  if (max_wind == min_wind) max_wind += 1.0f;

  float rain_scale = rainFullScale(max_precipitation);

  int plot_x = x_base + AXIS_GUTTER;
  int plot_y = y_base + CLOUD_BAND_HEIGHT + CLOUD_BAND_SPACING;
  int plot_w = w - 2 * AXIS_GUTTER;
  int plot_h = h - HOUR_LABEL_HEIGHT - CLOUD_BAND_HEIGHT - CLOUD_BAND_SPACING;

  if (plot_w <= 20 || plot_h <= 10) {
    drawMessage("Too small for graph.");
    return;
  }

  float x_step = (float)plot_w / (num_points - 1);

  // Cloud cover band: dithered black, denser the more overcast it gets
  for (int i = 0; i < num_points - 1; ++i) {
    int x1_pos = plot_x + round(i * x_step);
    int x2_pos = plot_x + round((i + 1) * x_step);

    float coverage = forecast.hourlyCloudCoverage[i];
    int density;
    if (coverage < 12.5f) {
      density = DENSITY_NONE;
    } else if (coverage < 37.5f) {
      density = DENSITY_LIGHT;
    } else if (coverage < 62.5f) {
      density = DENSITY_MEDIUM;
    } else if (coverage < 87.5f) {
      density = DENSITY_HEAVY;
    } else {
      density = DENSITY_SOLID;
    }

    fillDitheredRect(x1_pos, y_base, x2_pos - x1_pos, CLOUD_BAND_HEIGHT, CLOUD_COLOR, density);
  }
  display.drawRect(plot_x, y_base, plot_w, CLOUD_BAND_HEIGHT, GxEPD_BLACK);
  drawCloudIcon(plot_x - UNIT, y_base + CLOUD_BAND_HEIGHT / 2);

  // Horizontal guides at the midpoint and quarters, kept faint so the series stay dominant
  for (int i = 1; i < 4; i++) {
    drawDottedHLine(plot_x, plot_y + round(plot_h * i / 4.0f), plot_w, GxEPD_BLACK);
  }

  // Precipitation bars sit behind the series, in a softened blue
  int rain_area_h = round(plot_h * RAIN_AREA_FRACTION);
  int lastRainLabelRight = 0;
  for (int i = 0; i < num_points; ++i) {
    if (forecast.hourlyPrecipitation[i] > 0.0f) {
      int x_center = plot_x + round(i * x_step);
      int bar_width = max(3, (int)(x_step * 0.5f));
      int bar_height = round(min(forecast.hourlyPrecipitation[i] / rain_scale, 1.0f) * rain_area_h);

      int bar_x = x_center - bar_width / 2;
      int bar_y = plot_y + plot_h - bar_height;

      bar_x = constrain(bar_x, plot_x, plot_x + plot_w - bar_width);
      bar_y = constrain(bar_y, plot_y, plot_y + plot_h);
      bar_height = constrain(bar_height, 0, plot_y + plot_h - bar_y);

      fillDitheredRect(bar_x, bar_y, bar_width, bar_height, PRECIPITATION_COLOR, DENSITY_HEAVY);

      // Millimetres printed above the notable bars, so the amount is readable without a third axis
      if (forecast.hourlyPrecipitation[i] >= 0.3f) {
        String mmLabel = String(forecast.hourlyPrecipitation[i], 1);
        int mmWidth = textWidth(mmLabel, labelFont);
        int mmX = constrain(x_center - mmWidth / 2, plot_x + 2, plot_x + plot_w - mmWidth - 2);
        if (mmX >= lastRainLabelRight) {
          // Clear the label box first: without it the digits disappear into the series behind them
          display.fillRect(mmX - 2, bar_y - 16, mmWidth + 4, 15, GxEPD_WHITE);
          drawText(mmLabel, mmX, bar_y - 4, labelFont, PRECIPITATION_COLOR);
          lastRainLabelRight = mmX + mmWidth + UNIT / 2;
        }
      }
    }
  }

  // Hour ticks and labels every three hours
  for (int i = 0; i < num_points; i += 3) {
    int x_pos = plot_x + round(i * x_step);
    if (i > 0) {
      for (int y = plot_y; y < plot_y + plot_h; y += 4) {
        display.drawPixel(x_pos, y, GxEPD_BLACK);
      }
    }

    String hourLabel = forecast.hourlyTime[i];
    int labelWidth = textWidth(hourLabel, labelFont);
    int labelX = constrain(x_pos - labelWidth / 2, x_base, x_base + w - labelWidth);
    drawText(hourLabel, labelX, plot_y + plot_h + HOUR_LABEL_HEIGHT - UNIT / 2, labelFont, GxEPD_BLACK);
  }

  // Axis scales: temperature left, wind right, both with max / mid / min aligned to the guides
  gfx.setFont(labelFont);
  int labelAscent = gfx.getFontAscent();

  const float fractions[] = {0.0f, 0.5f, 1.0f};
  for (int i = 0; i < 3; i++) {
    float fraction = fractions[i];
    int label_y = plot_y + round(plot_h * (1.0f - fraction)) + labelAscent / 2;
    label_y = constrain(label_y, plot_y + labelAscent, plot_y + plot_h);

    bool isTop = fraction == 1.0f;

    String tempLabel = String(min_temp + (max_temp - min_temp) * fraction, 0) + (isTop ? "°C" : "");
    drawText(tempLabel, plot_x - textWidth(tempLabel, labelFont) - UNIT, label_y, labelFont, GxEPD_BLACK);

    String windLabel = String(min_wind + (max_wind - min_wind) * fraction, 0) + (isTop ? " m/s" : "");
    drawText(windLabel, plot_x + plot_w + UNIT, label_y, labelFont, GxEPD_BLACK);
  }

  drawSeries(forecast.hourlyWindGusts, num_points, min_wind, max_wind, plot_x, plot_y, plot_w, plot_h, 3, GUSTS_COLOR,
             true, false);
  drawSeries(forecast.hourlyWindSpeeds, num_points, min_wind, max_wind, plot_x, plot_y, plot_w, plot_h, 3, WIND_COLOR,
             false, false);
  drawSeries(forecast.hourlyTemperatures, num_points, min_temp, max_temp, plot_x, plot_y, plot_w, plot_h, 3,
             TEMPERATURE_COLOR, false, true);

  display.drawRect(plot_x, plot_y, plot_w, plot_h, GxEPD_BLACK);

  String lastUpdateStr = forecast.lastUpdateTime;
  int lastUpdateMinutes = parseHHMMtoMinutes(lastUpdateStr);
  int now_x = -1;

  if (lastUpdateMinutes != -1 && num_points > 1) {
    for (int i = 0; i < num_points; ++i) {
      int currentMinutes = parseHHMMtoMinutes(forecast.hourlyTime[i]);
      if (currentMinutes == -1) continue;

      if (lastUpdateMinutes == currentMinutes) {
        now_x = plot_x + round(i * x_step);
        break;
      }

      if (i < num_points - 1) {
        int nextMinutes = parseHHMMtoMinutes(forecast.hourlyTime[i + 1]);
        if (nextMinutes != -1 && lastUpdateMinutes > currentMinutes && lastUpdateMinutes < nextMinutes) {
          float fraction = (float)(lastUpdateMinutes - currentMinutes) / (nextMinutes - currentMinutes);
          now_x = plot_x + round((i + fraction) * x_step);
          break;
        }
      }
    }
  }

  if (now_x != -1 && now_x >= plot_x && now_x <= plot_x + plot_w) {
    display.drawFastVLine(now_x, plot_y, plot_h, NOW_COLOR);

    // Small marker on the top edge instead of another label inside the plot
    display.fillTriangle(now_x - 5, plot_y - 1, now_x + 5, plot_y - 1, now_x, plot_y + 6, NOW_COLOR);
  }
}

int MeteogramScreen::nextRefreshInSeconds() { return 1800; }
