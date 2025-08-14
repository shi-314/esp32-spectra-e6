#include "MeteogramWeatherScreen.h"

#include <algorithm>
#include <vector>

#include "battery.h"

MeteogramWeatherScreen::MeteogramWeatherScreen(DisplayType &display, const WeatherForecast &forecast)
    : display(display),
      forecast(forecast),
      primaryFont(u8g2_font_helvR14_tf),
      secondaryFont(u8g2_font_helvR10_tf),
      smallFont(u8g2_font_micro_tr),
      labelFont(u8g2_font_nokiafc22_tn) {
  gfx.begin(display);
}

int MeteogramWeatherScreen::parseHHMMtoMinutes(const String &hhmm) {
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

uint16_t MeteogramWeatherScreen::getTemperatureColor(float temperature) {
  if (temperature <= 0.0f) {
    return GxEPD_BLUE;  // Very cold - blue
  } else if (temperature <= 10.0f) {
    return GxEPD_DARKGREY;  // Cold - dark grey
  } else if (temperature <= 20.0f) {
    return GxEPD_BLACK;  // Moderate - black
  } else if (temperature <= 30.0f) {
    return GxEPD_ORANGE;  // Warm - orange
  } else {
    return GxEPD_RED;  // Hot - red
  }
}

void MeteogramWeatherScreen::drawThickLine(int x0, int y0, int x1, int y1, uint16_t color, int thickness) {
  // Draw multiple parallel lines to create thickness
  for (int offset = -(thickness / 2); offset <= thickness / 2; offset++) {
    if (abs(x1 - x0) > abs(y1 - y0)) {
      // More horizontal - vary y
      display.drawLine(x0, y0 + offset, x1, y1 + offset, color);
    } else {
      // More vertical - vary x
      display.drawLine(x0 + offset, y0, x1 + offset, y1, color);
    }
  }
}

void MeteogramWeatherScreen::drawDottedLine(int x0, int y0, int x1, int y1, uint16_t color) {
  int dx = abs(x1 - x0);
  int dy = abs(y1 - y0);
  int sx = x0 < x1 ? 1 : -1;
  int sy = y0 < y1 ? 1 : -1;
  int err = dx - dy;

  int x = x0;
  int y = y0;
  int dotCount = 0;
  const int dotLength = 4;  // Longer dots for visibility
  const int gapLength = 3;  // Longer gaps

  while (true) {
    if (dotCount < dotLength) {
      // Draw thicker dots with 2x2 pixel squares
      display.drawPixel(x, y, color);
      display.drawPixel(x + 1, y, color);
      display.drawPixel(x, y + 1, color);
      display.drawPixel(x + 1, y + 1, color);
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

void MeteogramWeatherScreen::render() {
  Serial.println("Displaying meteogram screen");

  display.init(115200);
  // display.setRotation(1);

  String batteryStatus = getBatteryStatus();
  String windDisplay = String(forecast.currentWindSpeed, 1) + " - " + String(forecast.currentWindGusts, 1) + " m/s";

  gfx.setFontMode(1);
  // gfx.setFontDirection(0);
  gfx.setForegroundColor(GxEPD_BLACK);
  gfx.setBackgroundColor(GxEPD_WHITE);
  gfx.setFont(secondaryFont);

  int text_height = gfx.getFontAscent() - gfx.getFontDescent();

  int wind_y = display.height() - 3;
  int temp_y = wind_y - text_height - 8;

  // Utilize more space on the large 7.3" display
  int meteogramX = 10;                        // Small left margin
  int meteogramY = 5;                         // Small top margin
  int meteogramW = display.width() - 20;      // Leave margins on both sides
  int meteogramH = temp_y - meteogramY - 20;  // More height for the graph

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    drawMeteogram(meteogramX, meteogramY, meteogramW, meteogramH);

    gfx.setFont(primaryFont);
    gfx.setCursor(6, temp_y);

    String temperatureDisplay = String(forecast.currentTemperature, 1) + " °C";
    gfx.print(temperatureDisplay);

    int temp_width = gfx.getUTF8Width(temperatureDisplay.c_str());

    gfx.setFont(secondaryFont);
    gfx.setCursor(6 + temp_width + 8, temp_y);
    gfx.print(" " + forecast.currentWeatherDescription);

    gfx.setCursor(6, wind_y);
    gfx.print(windDisplay);

    gfx.setForegroundColor(GxEPD_BLACK);
    gfx.setFont(smallFont);
    int battery_width = gfx.getUTF8Width(batteryStatus.c_str());
    int battery_height = gfx.getFontAscent() - gfx.getFontDescent();
    gfx.setCursor(display.width() - battery_width - 2, display.height() - 1);
    gfx.print(batteryStatus);
  } while (display.nextPage());
  display.hibernate();
  Serial.println("Display updated");
}

void MeteogramWeatherScreen::drawMeteogram(int x_base, int y_base, int w, int h) {
  gfx.setFont(smallFont);
  gfx.setFontMode(1);
  gfx.setForegroundColor(GxEPD_BLACK);
  gfx.setBackgroundColor(GxEPD_WHITE);

  if (forecast.hourlyTemperatures.empty() || forecast.hourlyWindSpeeds.empty() || forecast.hourlyWindGusts.empty() ||
      forecast.hourlyTime.empty() || forecast.hourlyPrecipitation.empty() || forecast.hourlyCloudCoverage.empty()) {
    gfx.setCursor(x_base, y_base + h / 2);
    gfx.print("No meteogram data.");
    return;
  }

  int num_points = std::min({(int)forecast.hourlyTemperatures.size(), (int)forecast.hourlyWindSpeeds.size(),
                             (int)forecast.hourlyWindGusts.size(), (int)forecast.hourlyTime.size(),
                             (int)forecast.hourlyPrecipitation.size(), (int)forecast.hourlyCloudCoverage.size(), 24});
  if (num_points <= 1) {
    gfx.setFont(labelFont);
    gfx.setCursor(x_base, y_base + h / 2);
    gfx.print("Not enough data.");
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
  if (max_precipitation == 0.0f) max_precipitation = 1.0f;

  uint16_t label_w, label_h;
  gfx.setFont(labelFont);
  label_w = gfx.getUTF8Width("99");
  label_h = gfx.getFontAscent() - gfx.getFontDescent();

  int left_padding = label_w + 10;  // More padding for labels
  int right_padding = label_w + 10;
  int bottom_padding = label_h + 12;  // More space for legend

  const int cloud_bar_height = 12;  // Thicker cloud bar
  const int cloud_bar_spacing = 4;

  int plot_x = x_base + left_padding;
  int plot_y = y_base + cloud_bar_height + cloud_bar_spacing;
  int plot_w = w - left_padding - right_padding;
  int plot_h = h - bottom_padding - cloud_bar_height - cloud_bar_spacing;

  if (plot_w <= 20 || plot_h <= 10) {
    gfx.setFont(labelFont);
    gfx.setCursor(x_base, y_base + h / 2);
    gfx.print("Too small for graph.");
    return;
  }

  // Draw cloud coverage bar on top
  float x_step = (float)plot_w / (num_points - 1);
  for (int i = 0; i < num_points - 1; ++i) {
    int x1_pos = plot_x + round(i * x_step);
    int x2_pos = plot_x + round((i + 1) * x_step);
    int segment_width = x2_pos - x1_pos;

    // Get cloud coverage color based on percentage - enhanced with colors
    uint16_t cloudColor;
    float coverage = forecast.hourlyCloudCoverage[i];
    if (coverage < 20.0f) {
      cloudColor = GxEPD_WHITE;  // Clear sky
    } else if (coverage < 40.0f) {
      cloudColor = GxEPD_LIGHTGREY;  // Partly cloudy
    } else if (coverage < 60.0f) {
      cloudColor = GxEPD_DARKGREY;  // Mostly cloudy
    } else if (coverage < 80.0f) {
      cloudColor = GxEPD_BLUE;  // Overcast with blue tint
    } else {
      cloudColor = GxEPD_BLACK;  // Heavy overcast
    }

    display.fillRect(x1_pos, y_base, segment_width, cloud_bar_height, cloudColor);
  }

  // Draw thicker border around cloud coverage bar
  display.drawRect(plot_x, y_base, plot_w, cloud_bar_height, GxEPD_BLACK);
  display.drawRect(plot_x + 1, y_base + 1, plot_w - 2, cloud_bar_height - 2, GxEPD_BLACK);

  for (int i = 0; i < num_points; ++i) {
    if (forecast.hourlyPrecipitation[i] > 0.0f) {
      int x_center = plot_x + round(i * x_step);
      int bar_width = max(3, (int)(x_step * 0.7f));  // Wider precipitation bars
      int bar_height = round((forecast.hourlyPrecipitation[i] / max_precipitation) * plot_h);

      int bar_x = x_center - bar_width / 2;
      int bar_y = plot_y + plot_h - bar_height;

      bar_x = constrain(bar_x, plot_x, plot_x + plot_w - bar_width);
      bar_y = constrain(bar_y, plot_y, plot_y + plot_h);
      bar_height = constrain(bar_height, 0, plot_y + plot_h - bar_y);

      display.fillRect(bar_x, bar_y, bar_width, bar_height, GxEPD_BLUE);
    }
  }

  gfx.setFont(labelFont);
  gfx.setForegroundColor(GxEPD_BLACK);

  String temp_labels[] = {String(max_temp, 0), String(min_temp, 0)};
  String wind_labels[] = {String(max_wind, 0), String(min_wind, 0)};

  // Proper vertical alignment: top label at top of plot, bottom label at bottom of plot
  int font_ascent = gfx.getFontAscent();
  int font_descent = gfx.getFontDescent();
  int y_positions[] = {plot_y + font_ascent, plot_y + plot_h - font_descent};

  for (int i = 0; i < 2; i++) {
    label_w = gfx.getUTF8Width(temp_labels[i].c_str());
    gfx.setCursor(plot_x - label_w - 3, y_positions[i]);
    gfx.print(temp_labels[i]);

    gfx.setCursor(plot_x + plot_w + 3, y_positions[i]);
    gfx.print(wind_labels[i]);
  }

  // Add color legend at the bottom
  int legend_y = plot_y + plot_h + bottom_padding - 2;
  int legend_x = plot_x;

  // Temperature legend (thick solid line)
  drawThickLine(legend_x, legend_y - 8, legend_x + 20, legend_y - 8, GxEPD_RED, 3);
  gfx.setFont(labelFont);
  gfx.setCursor(legend_x + 25, legend_y - 4);
  gfx.print("Temp");

  // Wind speed legend (thick dotted line)
  legend_x += 80;
  for (int x = 0; x < 20; x += 5) {
    display.fillRect(legend_x + x, legend_y - 9, 3, 2, GxEPD_YELLOW);
  }
  gfx.setCursor(legend_x + 25, legend_y - 4);
  gfx.print("Wind");

  // Wind gust legend (thick dotted line)
  legend_x += 80;
  for (int x = 0; x < 20; x += 5) {
    display.fillRect(legend_x + x, legend_y - 9, 3, 2, GxEPD_ORANGE);
  }
  gfx.setCursor(legend_x + 25, legend_y - 4);
  gfx.print("Gust");

  // Precipitation legend (thicker bar)
  legend_x += 80;
  display.fillRect(legend_x, legend_y - 12, 6, 8, GxEPD_BLUE);
  gfx.setCursor(legend_x + 10, legend_y - 4);
  gfx.print("Rain");

  for (int i = 0; i < num_points - 1; ++i) {
    int x1 = plot_x + round(i * x_step);
    int x2 = plot_x + round((i + 1) * x_step);

    int y1_temp =
        plot_y + plot_h - round(((forecast.hourlyTemperatures[i] - min_temp) / (max_temp - min_temp)) * plot_h);
    int y2_temp =
        plot_y + plot_h - round(((forecast.hourlyTemperatures[i + 1] - min_temp) / (max_temp - min_temp)) * plot_h);

    int y1_wind = plot_y + plot_h - round(((forecast.hourlyWindSpeeds[i] - min_wind) / (max_wind - min_wind)) * plot_h);
    int y2_wind =
        plot_y + plot_h - round(((forecast.hourlyWindSpeeds[i + 1] - min_wind) / (max_wind - min_wind)) * plot_h);

    int y1_gust = plot_y + plot_h - round(((forecast.hourlyWindGusts[i] - min_wind) / (max_wind - min_wind)) * plot_h);
    int y2_gust =
        plot_y + plot_h - round(((forecast.hourlyWindGusts[i + 1] - min_wind) / (max_wind - min_wind)) * plot_h);

    // Temperature line with color based on average temperature - now thicker
    float avgTemp = (forecast.hourlyTemperatures[i] + forecast.hourlyTemperatures[i + 1]) / 2.0f;
    uint16_t tempColor = getTemperatureColor(avgTemp);
    drawThickLine(constrain(x1, plot_x, plot_x + plot_w - 1), constrain(y1_temp, plot_y, plot_y + plot_h - 1),
                  constrain(x2, plot_x, plot_x + plot_w - 1), constrain(y2_temp, plot_y, plot_y + plot_h - 1),
                  tempColor, 3);  // 3 pixel thick line
    // Wind speed line in yellow
    drawDottedLine(constrain(x1, plot_x, plot_x + plot_w - 1), constrain(y1_wind, plot_y, plot_y + plot_h - 1),
                   constrain(x2, plot_x, plot_x + plot_w - 1), constrain(y2_wind, plot_y, plot_y + plot_h - 1),
                   GxEPD_YELLOW);
    // Wind gust line in orange
    drawDottedLine(constrain(x1, plot_x, plot_x + plot_w - 1), constrain(y1_gust, plot_y, plot_y + plot_h - 1),
                   constrain(x2, plot_x, plot_x + plot_w - 1), constrain(y2_gust, plot_y, plot_y + plot_h - 1),
                   GxEPD_ORANGE);
  }

  // Draw thicker main plot border
  display.drawRect(plot_x, plot_y, plot_w, plot_h, GxEPD_BLACK);
  display.drawRect(plot_x + 1, plot_y + 1, plot_w - 2, plot_h - 2, GxEPD_BLACK);

  String lastUpdateStr = forecast.lastUpdateTime;
  int lastUpdateMinutes = parseHHMMtoMinutes(lastUpdateStr);
  int final_line_x = -1;

  if (lastUpdateMinutes != -1 && num_points > 1) {
    for (int i = 0; i < num_points; ++i) {
      int currentMinutes = parseHHMMtoMinutes(forecast.hourlyTime[i]);
      if (currentMinutes == -1) continue;

      if (lastUpdateMinutes == currentMinutes) {
        final_line_x = plot_x + round(i * x_step);
        break;
      }

      if (i < num_points - 1) {
        int nextMinutes = parseHHMMtoMinutes(forecast.hourlyTime[i + 1]);
        if (nextMinutes != -1 && lastUpdateMinutes > currentMinutes && lastUpdateMinutes < nextMinutes) {
          float fraction = (float)(lastUpdateMinutes - currentMinutes) / (nextMinutes - currentMinutes);
          final_line_x = plot_x + round((i + fraction) * x_step);
          break;
        }
      }
    }
  }

  if (final_line_x != -1 && final_line_x >= plot_x && final_line_x <= plot_x + plot_w) {
    // Draw thicker current time indicator
    display.drawFastVLine(final_line_x, plot_y, plot_h, GxEPD_BLACK);
    display.drawFastVLine(final_line_x + 1, plot_y, plot_h, GxEPD_BLACK);
    display.drawFastVLine(final_line_x - 1, plot_y, plot_h, GxEPD_BLACK);

    gfx.setFont(labelFont);
    label_w = gfx.getUTF8Width(lastUpdateStr.c_str());
    int time_x = constrain(final_line_x - label_w / 2, x_base, x_base + w - label_w);
    gfx.setCursor(time_x, plot_y + plot_h + bottom_padding - 4);
    gfx.print(lastUpdateStr);
  }
}

int MeteogramWeatherScreen::nextRefreshInSeconds() { return 900; }
