#ifndef CURRENT_WEATHER_SCREEN_H
#define CURRENT_WEATHER_SCREEN_H

#include <U8g2_for_Adafruit_GFX.h>

#include "DisplayType.h"
#include "OpenMeteoAPI.h"
#include "Screen.h"

class CurrentWeatherScreen : public Screen {
 private:
  DisplayType& display;
  U8G2_FOR_ADAFRUIT_GFX gfx;
  const WeatherForecast& forecast;
  const String cityName;
  const String countryCode;

  const uint8_t* primaryFont;
  const uint8_t* mediumFont;
  const uint8_t* smallFont;

 public:
  CurrentWeatherScreen(DisplayType& display, const WeatherForecast& forecast, const String& cityName,
                       const String& countryCode);

  void render() override;
  int nextRefreshInSeconds() override;
};

#endif
