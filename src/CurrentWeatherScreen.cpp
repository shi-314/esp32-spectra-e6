#include "CurrentWeatherScreen.h"

#include "battery.h"

CurrentWeatherScreen::CurrentWeatherScreen(DisplayType& display, const WeatherForecast& forecast,
                                           const String& cityName, const String& countryCode)
    : display(display),
      forecast(forecast),
      cityName(cityName),
      countryCode(countryCode),
      primaryFont(u8g2_font_helvB24_tf),
      mediumFont(u8g2_font_helvR12_tr),
      smallFont(u8g2_font_helvR08_tr) {
  gfx.begin(display);
}

void CurrentWeatherScreen::render() {
  Serial.println("Displaying current weather screen");

  display.init(115200);
  display.setRotation(1);

  gfx.setFontMode(1);
  gfx.setForegroundColor(GxEPD_BLACK);
  gfx.setBackgroundColor(GxEPD_WHITE);

  if (forecast.lastUpdateTime.length() == 0) {
    Serial.println("Warning: Forecast data is invalid, displaying error message");
    display.setFullWindow();
    display.firstPage();
    do {
      display.fillScreen(GxEPD_WHITE);
      gfx.setFont(smallFont);
      gfx.setCursor(10, 30);
      gfx.print("Weather data unavailable");
    } while (display.nextPage());
    return;
  }

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    int topMargin = 5;
    int usableHeight = display.height() - topMargin;

    gfx.setFont(primaryFont);
    String temperatureText = String(forecast.currentTemperature, 1) + " °C";
    int temperatureWidth = gfx.getUTF8Width(temperatureText.c_str());
    int temperatureAscent = gfx.getFontAscent();
    int temperatureDescent = gfx.getFontDescent();

    gfx.setFont(mediumFont);
    String weatherDescription = forecast.currentWeatherCodeDescription;
    int descriptionWidth = gfx.getUTF8Width(weatherDescription.c_str());
    int descriptionAscent = gfx.getFontAscent();
    int descriptionDescent = gfx.getFontDescent();

    int textSpacing = 4;
    int totalGroupHeight =
        temperatureAscent - temperatureDescent + textSpacing + descriptionAscent - descriptionDescent;

    int centerY = topMargin + usableHeight / 2;
    int groupTopY = centerY - totalGroupHeight / 2;

    int temperatureX = (display.width() - temperatureWidth) / 2;
    int temperatureY = groupTopY + temperatureAscent;

    gfx.setFont(primaryFont);
    gfx.setCursor(temperatureX, temperatureY);
    gfx.print(temperatureText);

    int descriptionX = (display.width() - descriptionWidth) / 2;
    int descriptionY = temperatureY - temperatureDescent + textSpacing + descriptionAscent;

    gfx.setFont(mediumFont);
    gfx.setCursor(descriptionX, descriptionY);
    gfx.print(weatherDescription);

    gfx.setFont(smallFont);
    int smallFontHeight = gfx.getFontAscent() - gfx.getFontDescent();

    gfx.setCursor(2, topMargin + smallFontHeight + 2);
    gfx.print(forecast.lastUpdateTime);

    String locationText = cityName;
    if (countryCode.length() > 0) {
      locationText += ", " + countryCode;
    }

    int locationWidth = gfx.getUTF8Width(locationText.c_str());
    gfx.setCursor(display.width() - locationWidth - 2, topMargin + smallFontHeight + 2);
    gfx.print(locationText);

    String windText = String(forecast.currentWindSpeed, 1) + " - " + String(forecast.currentWindGusts, 1) + " m/s";
    gfx.setCursor(2, display.height() - 2);
    gfx.print(windText);

    String batteryStatus = getBatteryStatus();
    int batteryWidth = gfx.getUTF8Width(batteryStatus.c_str());
    gfx.setCursor(display.width() - batteryWidth - 2, display.height() - 2);
    gfx.print(batteryStatus);
  } while (display.nextPage());

  display.hibernate();
  Serial.println("Current weather display updated");
}

int CurrentWeatherScreen::nextRefreshInSeconds() { return 900; }
