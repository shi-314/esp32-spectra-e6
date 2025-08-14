#include "OpenMeteoAPI.h"

#include <time.h>

OpenMeteoAPI::OpenMeteoAPI() {}

WeatherForecast OpenMeteoAPI::getForecast(float latitude, float longitude) const {
  WeatherForecast forecast;

  HTTPClient http;
  String url = String(forecastEndpoint) + "?latitude=" + String(latitude, 6) + "&longitude=" + String(longitude, 6) +
               "&hourly=temperature_2m,precipitation,wind_speed_10m,wind_gusts_10m,cloud_cover_low" +
               "&current=wind_speed_10m,wind_gusts_10m,temperature_2m,weather_code,wind_direction_10m" +
               "&forecast_days=1" + "&timezone=auto";

  http.begin(url);
  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    Serial.println("Failed to get weather data");
    http.end();
    return forecast;
  }

  String payload = http.getString();
  forecast.apiPayload = payload;

  DynamicJsonDocument doc(16384);
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.print("JSON parsing failed: ");
    Serial.println(error.c_str());
    http.end();
    return forecast;
  }

  forecast.currentTemperature = doc["current"]["temperature_2m"];
  forecast.currentWeatherCode = doc["current"]["weather_code"];
  forecast.currentWeatherCodeDescription = getWeatherDescription(forecast.currentWeatherCode);
  String timeStr = doc["current"]["time"].as<String>();

  float windSpeedKmh = doc["current"]["wind_speed_10m"].as<float>();
  forecast.currentWindSpeed = windSpeedKmh / 3.6;  // Convert km/h to m/s

  forecast.currentWindDirection = doc["current"]["wind_direction_10m"];

  float windGustsKmh = doc["current"]["wind_gusts_10m"].as<float>();
  forecast.currentWindGusts = windGustsKmh / 3.6;  // Convert km/h to m/s

  forecast.currentWeatherDescription = getWeatherDescription(forecast.currentWeatherCode);

  struct tm timeinfo;
  strptime(timeStr.c_str(), "%Y-%m-%dT%H:%M", &timeinfo);
  char timeBuffer[6];
  strftime(timeBuffer, sizeof(timeBuffer), "%H:%M", &timeinfo);
  forecast.lastUpdateTime = String(timeBuffer);

  JsonArray hourly_temp_array = doc["hourly"]["temperature_2m"].as<JsonArray>();
  for (JsonVariant v : hourly_temp_array) {
    forecast.hourlyTemperatures.push_back(v.as<float>());
  }

  JsonArray hourly_wind_array = doc["hourly"]["wind_speed_10m"].as<JsonArray>();
  for (JsonVariant v : hourly_wind_array) {
    forecast.hourlyWindSpeeds.push_back(v.as<float>() / 3.6);
  }

  JsonArray hourly_wind_gusts_array = doc["hourly"]["wind_gusts_10m"].as<JsonArray>();
  for (JsonVariant v : hourly_wind_gusts_array) {
    forecast.hourlyWindGusts.push_back(v.as<float>() / 3.6);
  }

  JsonArray hourly_time_array = doc["hourly"]["time"].as<JsonArray>();
  for (JsonVariant v : hourly_time_array) {
    String fullTimestamp = v.as<String>();
    forecast.hourlyTime.push_back(fullTimestamp.substring(11, 16));
  }

  JsonArray hourly_precipitation_array = doc["hourly"]["precipitation"].as<JsonArray>();
  for (JsonVariant v : hourly_precipitation_array) {
    forecast.hourlyPrecipitation.push_back(v.as<float>());
  }

  JsonArray hourly_cloud_array = doc["hourly"]["cloud_cover_low"].as<JsonArray>();
  for (JsonVariant v : hourly_cloud_array) {
    forecast.hourlyCloudCoverage.push_back(v.as<float>());
  }

  http.end();
  return forecast;
}

String OpenMeteoAPI::getWeatherDescription(int weatherCode) const {
  // https://www.nodc.noaa.gov/archive/arc0021/0002199/1.1/data/0-data/HTML/WMO-CODE/WMO4677.HTM
  switch (weatherCode) {
    // Clear and cloudy conditions
    case 0:
    case 1:
    case 2:
    case 3:
      return "Clear";

    // Visibility issues
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
      return "Dusty";
    case 10:
    case 11:
    case 12:
      return "Foggy";
    case 40:
    case 41:
    case 42:
    case 43:
    case 44:
    case 45:
    case 46:
    case 47:
    case 48:
    case 49:
      return "Foggy";

    // Distant weather
    case 13:
    case 14:
    case 15:
    case 16:
      return "Distant storms";
    case 17:
    case 18:
    case 19:
      return "Stormy";

    // Past weather (preceding hour)
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
    case 25:
    case 26:
    case 27:
    case 28:
    case 29:
      return "Recently wet";

    // Dust/sand storms and blowing snow
    case 30:
    case 31:
    case 32:
    case 33:
    case 34:
    case 35:
      return "Dust storm";
    case 36:
    case 37:
    case 38:
    case 39:
      return "Blowing snow";

    // Light precipitation
    case 50:
    case 51:
    case 58:
      return "Light drizzle";
    case 52:
    case 53:
    case 59:
      return "Drizzle";
    case 54:
    case 55:
      return "Heavy drizzle";
    case 56:
    case 57:
      return "Freezing drizzle";

    case 60:
    case 61:
      return "Light rain";
    case 62:
    case 63:
      return "Rain";
    case 64:
    case 65:
      return "Heavy rain";
    case 66:
    case 67:
      return "Freezing rain";
    case 68:
    case 69:
      return "Rain and snow";

    // Snow
    case 70:
    case 71:
      return "Light snow";
    case 72:
    case 73:
      return "Snow";
    case 74:
    case 75:
      return "Heavy snow";
    case 76:
    case 77:
    case 78:
      return "Snow crystals";
    case 79:
      return "Ice pellets";

    // Showers
    case 80:
      return "Light showers";
    case 81:
    case 82:
      return "Heavy showers";
    case 83:
    case 84:
      return "Mixed showers";
    case 85:
      return "Snow showers";
    case 86:
      return "Heavy snow showers";
    case 87:
    case 88:
    case 89:
    case 90:
      return "Hail showers";

    // Thunderstorms
    case 91:
    case 92:
    case 93:
    case 94:
      return "Recent storms";
    case 95:
      return "Thunderstorm";
    case 96:
    case 99:
      return "Thunderstorm with hail";
    case 97:
      return "Heavy thunderstorm";
    case 98:
      return "Dust thunderstorm";

    default:
      return "Unknown";
  }
}

GeocodingResult OpenMeteoAPI::getLocationByCity(const String& cityName, const String& countryCode) const {
  GeocodingResult result;

  HTTPClient http;
  String url = String(geocodingEndpoint) + "?name=" + cityName + "&count=1&language=en&format=json";

  if (countryCode.length() > 0) {
    url += "&countryCode=" + countryCode;
  }

  http.begin(url);
  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    Serial.println("Failed to get geocoding data");
    http.end();
    return result;
  }

  String payload = http.getString();
  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.print("Geocoding JSON parsing failed: ");
    Serial.println(error.c_str());
    http.end();
    return result;
  }

  JsonArray results = doc["results"].as<JsonArray>();
  if (results.size() > 0) {
    JsonObject firstResult = results[0];

    result.name = firstResult["name"].as<String>();
    result.latitude = firstResult["latitude"].as<float>();
    result.longitude = firstResult["longitude"].as<float>();
    result.elevation = firstResult["elevation"].as<float>();
    result.countryCode = firstResult["country_code"].as<String>();
  }

  http.end();
  return result;
}