#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>

#include <vector>

struct GeocodingResult {
  String name;
  float latitude;
  float longitude;
  float elevation;
  String countryCode;
};

struct WeatherForecast {
  String lastUpdateTime;
  String lastUpdateDate;
  String currentTime;  // Local ISO timestamp (YYYY-MM-DDTHH:MM) of the current conditions

  // Local ISO timestamps for today and tomorrow, so the chart can shade every night it spans
  std::vector<String> sunrises;
  std::vector<String> sunsets;

  float currentTemperature;
  float currentApparentTemperature;
  String currentWeatherDescription;
  int currentWeatherCode;
  String currentWeatherCodeDescription;

  std::vector<float> hourlyTemperatures;
  std::vector<float> hourlyWindSpeeds;
  std::vector<float> hourlyWindGusts;
  std::vector<String> hourlyTime;  // Local ISO timestamps
  std::vector<float> hourlyPrecipitation;
  std::vector<float> hourlyCloudCover;

  String apiPayload;
};

class OpenMeteoAPI {
 public:
  OpenMeteoAPI();

  WeatherForecast getForecast(float latitude, float longitude) const;
  WeatherForecast parseForecast(const String& payload) const;
  GeocodingResult getLocationByCity(const String& cityName, const String& countryCode = "") const;

 private:
  // API settings
  const char* forecastEndpoint = "https://api.open-meteo.com/v1/forecast";
  const char* geocodingEndpoint = "https://geocoding-api.open-meteo.com/v1/search";

  String getWeatherDescription(int weatherCode) const;
};
