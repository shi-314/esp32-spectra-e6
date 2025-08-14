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

  float currentTemperature;
  float currentWindSpeed;
  float currentWindGusts;
  int currentWindDirection;
  String currentWeatherDescription;
  int currentWeatherCode;
  String currentWeatherCodeDescription;

  std::vector<float> hourlyTemperatures;
  std::vector<float> hourlyWindSpeeds;
  std::vector<float> hourlyWindGusts;
  std::vector<String> hourlyTime;
  std::vector<float> hourlyPrecipitation;
  std::vector<float> hourlyCloudCoverage;

  String apiPayload;
};

class OpenMeteoAPI {
 public:
  OpenMeteoAPI();

  WeatherForecast getForecast(float latitude, float longitude) const;
  GeocodingResult getLocationByCity(const String& cityName, const String& countryCode = "") const;

 private:
  // API settings
  const char* forecastEndpoint = "https://api.open-meteo.com/v1/forecast";
  const char* geocodingEndpoint = "https://geocoding-api.open-meteo.com/v1/search";

  String getWeatherDescription(int weatherCode) const;
};
