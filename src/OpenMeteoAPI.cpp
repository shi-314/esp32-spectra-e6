#include "OpenMeteoAPI.h"

#include <time.h>

OpenMeteoAPI::OpenMeteoAPI() {}

WeatherForecast OpenMeteoAPI::getForecast(float latitude, float longitude) const {
  HTTPClient http;
  // A rolling window from the previous hour to 24 hours ahead, so the chart always looks forward.
  // Daily values cover two days because that window usually spans a second sunrise.
  String url = String(forecastEndpoint) + "?latitude=" + String(latitude, 6) + "&longitude=" + String(longitude, 6) +
               "&hourly=temperature_2m,precipitation,wind_speed_10m,wind_gusts_10m,cloud_cover" +
               "&current=temperature_2m,apparent_temperature,weather_code" + "&daily=sunrise,sunset" +
               "&forecast_days=2" + "&past_hours=1" + "&forecast_hours=24" + "&wind_speed_unit=ms" + "&timezone=auto";

  http.begin(url);
  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    Serial.println("Failed to get weather data");
    http.end();
    return WeatherForecast();
  }

  String payload = http.getString();
  http.end();
  return parseForecast(payload);
}

namespace {
template <typename T>
void appendArray(JsonArray array, std::vector<T>& target) {
  for (JsonVariant v : array) {
    target.push_back(v.as<T>());
  }
}
}  // namespace

WeatherForecast OpenMeteoAPI::parseForecast(const String& payload) const {
  WeatherForecast forecast;
  forecast.apiPayload = payload;

  DynamicJsonDocument doc(16384);
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.print("JSON parsing failed: ");
    Serial.println(error.c_str());
    return forecast;
  }

  forecast.currentTemperature = doc["current"]["temperature_2m"];
  forecast.currentApparentTemperature = doc["current"]["apparent_temperature"];
  forecast.currentWeatherCode = doc["current"]["weather_code"];
  forecast.currentWeatherCodeDescription = getWeatherDescription(forecast.currentWeatherCode);
  forecast.currentWeatherDescription = forecast.currentWeatherCodeDescription;

  forecast.currentTime = doc["current"]["time"].as<String>();
  struct tm timeinfo = {};
  strptime(forecast.currentTime.c_str(), "%Y-%m-%dT%H:%M", &timeinfo);
  char timeBuffer[6];
  strftime(timeBuffer, sizeof(timeBuffer), "%H:%M", &timeinfo);
  forecast.lastUpdateTime = String(timeBuffer);

  char dateBuffer[16];
  strftime(dateBuffer, sizeof(dateBuffer), "%a %d %b", &timeinfo);
  forecast.lastUpdateDate = String(dateBuffer);

  appendArray(doc["daily"]["sunrise"].as<JsonArray>(), forecast.sunrises);
  appendArray(doc["daily"]["sunset"].as<JsonArray>(), forecast.sunsets);

  JsonObject hourly = doc["hourly"];
  appendArray(hourly["time"].as<JsonArray>(), forecast.hourlyTime);
  appendArray(hourly["temperature_2m"].as<JsonArray>(), forecast.hourlyTemperatures);
  appendArray(hourly["wind_speed_10m"].as<JsonArray>(), forecast.hourlyWindSpeeds);
  appendArray(hourly["wind_gusts_10m"].as<JsonArray>(), forecast.hourlyWindGusts);
  appendArray(hourly["precipitation"].as<JsonArray>(), forecast.hourlyPrecipitation);
  appendArray(hourly["cloud_cover"].as<JsonArray>(), forecast.hourlyCloudCover);

  return forecast;
}

float OpenMeteoAPI::getMoonPhase(float latitude, float longitude) const {
  HTTPClient http;
  // Open-Meteo gives one phase per day, so today's and tomorrow's are interpolated to the current time
  String url = String(forecastEndpoint) + "?latitude=" + String(latitude, 6) + "&longitude=" + String(longitude, 6) +
               "&daily=moon_phase" + "&current=is_day" + "&forecast_days=2" + "&timezone=auto";

  http.begin(url);
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.println("Failed to get moon phase");
    http.end();
    return NAN;
  }

  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, http.getString());
  http.end();

  JsonArray phases = doc["daily"]["moon_phase"].as<JsonArray>();
  String now = doc["current"]["time"].as<String>();
  if (error || phases.size() < 2 || now.length() < 16) {
    Serial.println("Moon phase response incomplete");
    return NAN;
  }

  float today = phases[0].as<float>();
  float tomorrow = phases[1].as<float>();
  if (tomorrow < today) tomorrow += 1.0f;  // A new moon falls between the two days

  float dayFraction = (now.substring(11, 13).toInt() * 60 + now.substring(14, 16).toInt()) / 1440.0f;
  float phase = today + (tomorrow - today) * dayFraction;
  return phase - floorf(phase);
}

String OpenMeteoAPI::getWeatherDescription(int weatherCode) const {
  // Open-Meteo reports the WMO subset listed at https://open-meteo.com/en/docs#weather_variable_documentation
  switch (weatherCode) {
    case 0:
      return "Clear";
    case 1:
      return "Mostly clear";
    case 2:
      return "Partly cloudy";
    case 3:
      return "Overcast";
    case 45:
      return "Fog";
    case 48:
      return "Freezing fog";
    case 51:
      return "Light drizzle";
    case 53:
      return "Drizzle";
    case 55:
      return "Heavy drizzle";
    case 56:
    case 57:
      return "Freezing drizzle";
    case 61:
      return "Light rain";
    case 63:
      return "Rain";
    case 65:
      return "Heavy rain";
    case 66:
    case 67:
      return "Freezing rain";
    case 71:
      return "Light snow";
    case 73:
      return "Snow";
    case 75:
      return "Heavy snow";
    case 77:
      return "Snow grains";
    case 80:
      return "Light showers";
    case 81:
      return "Showers";
    case 82:
      return "Heavy showers";
    case 85:
      return "Snow showers";
    case 86:
      return "Heavy snow showers";
    case 95:
      return "Thunderstorm";
    case 96:
    case 99:
      return "Thunderstorm, hail";
    default:
      return "Unknown";
  }
}

namespace {
// Percent-encodes everything but unreserved characters, so names like Würzburg survive the query string
String urlEncode(const String& text) {
  const char* hex = "0123456789ABCDEF";
  String encoded;
  for (unsigned i = 0; i < text.length(); i++) {
    uint8_t c = text.charAt(i);
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += (char)c;
    } else {
      encoded += '%';
      encoded += hex[c >> 4];
      encoded += hex[c & 0x0F];
    }
  }
  return encoded;
}
}  // namespace

GeocodingResult OpenMeteoAPI::getLocationByCity(const String& cityName, const String& countryCode) const {
  GeocodingResult result;

  HTTPClient http;
  String url = String(geocodingEndpoint) + "?name=" + urlEncode(cityName) + "&count=1&language=en&format=json";

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