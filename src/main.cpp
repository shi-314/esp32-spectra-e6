#include <Arduino.h>
#include <time.h>

#include <memory>

#include "ApplicationConfig.h"
#include "ApplicationConfigStorage.h"
#include "ChatGPTClient.h"
#include "ConfigurationScreen.h"
#include "ConfigurationServer.h"
#include "CurrentWeatherScreen.h"
#include "DisplayType.h"
#include "ImageScreen.h"
#include "MessageScreen.h"
#include "MeteogramWeatherScreen.h"
#include "OpenMeteoAPI.h"
#include "WiFiConnection.h"
#include "WifiErrorScreen.h"
#include "battery.h"
#include "boards.h"

std::unique_ptr<ApplicationConfig> appConfig;
ApplicationConfigStorage configStorage;

const String aiWeatherPrompt =
    "I will share a JSON payload with you from the Open Meteo API which has weather forecast data for the current "
    "day. You have to summarize it into one sentence:\n"
    "- 18 words or less\n"
    "- include the rough temperature in the sentence\n"
    "- forecast for the whole day is included in the sentence\n"
    "- use the time of the day to make the sentence more interesting, but don't mention the exact time\n"
    "- don't mention the location\n"
    "- only include the current weather and the forecast for the remaining day, not the past\n";

// Standard constructor for GxEPD2
DisplayType display(Epd2Type(EPD_CS, EPD_DC, EPD_RSET, EPD_BUSY));

OpenMeteoAPI openMeteoAPI;

void goToSleep(uint64_t sleepTimeInSeconds);
int displayCurrentScreen();
void cycleToNextScreen();
bool isButtonWakeup();
void updateConfiguration(const Configuration& config);
void initializeDefaultConfig();

void geocodeCurrentLocation() {
  if (strlen(appConfig->city) == 0) {
    Serial.println("No city configured, using default coordinates");
    return;
  }

  Serial.printf("Geocoding location: %s (%s)\n", appConfig->city,
                strlen(appConfig->countryCode) > 0 ? appConfig->countryCode : "");

  GeocodingResult location = openMeteoAPI.getLocationByCity(String(appConfig->city), String(appConfig->countryCode));
  if (location.name.length() > 0) {
    appConfig->latitude = location.latitude;
    appConfig->longitude = location.longitude;

    strncpy(appConfig->city, location.name.c_str(), sizeof(appConfig->city) - 1);
    strncpy(appConfig->countryCode, location.countryCode.c_str(), sizeof(appConfig->countryCode) - 1);

    Serial.printf("Geocoded successfully: %s -> (%f, %f)\n", appConfig->city, appConfig->latitude,
                  appConfig->longitude);
  } else {
    Serial.printf("Geocoding failed for %s, using fallback coordinates\n", appConfig->city);
  }
}

bool isButtonWakeup() {
  esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();
  Serial.printf("Wakeup cause: %d (EXT0=%d, TIMER=%d)\n", wakeupReason, ESP_SLEEP_WAKEUP_EXT0, ESP_SLEEP_WAKEUP_TIMER);
  return (wakeupReason == ESP_SLEEP_WAKEUP_EXT0);
}

void cycleToNextScreen() {
  appConfig->currentScreenIndex = (appConfig->currentScreenIndex + 1) % SCREEN_COUNT;

  // Skip MESSAGE_SCREEN if no OpenAI API key is configured
  if (appConfig->currentScreenIndex == MESSAGE_SCREEN && !appConfig->hasValidOpenaiApiKey()) {
    appConfig->currentScreenIndex = (appConfig->currentScreenIndex + 1) % SCREEN_COUNT;
  }

  Serial.println("Cycled to screen: " + String(appConfig->currentScreenIndex));

  // NVS disabled - configuration not saved
}

int displayCurrentScreen() {
  appConfig->currentScreenIndex = IMAGE_SCREEN;
  switch (appConfig->currentScreenIndex) {
    case CONFIG_SCREEN: {
      ConfigurationScreen configurationScreen(display);
      configurationScreen.render();

      Configuration currentConfig =
          Configuration(appConfig->wifiSSID, appConfig->wifiPassword, appConfig->openaiApiKey, appConfig->aiPromptStyle,
                        appConfig->city, appConfig->countryCode, appConfig->imageUrl);
      ConfigurationServer configurationServer(currentConfig);

      configurationServer.run(updateConfiguration);

      while (configurationServer.isRunning()) {
        configurationServer.handleRequests();

        if (digitalRead(BUTTON_1) == LOW) {
          delay(50);
          if (digitalRead(BUTTON_1) == LOW) {
            Serial.println("Button pressed - exiting configuration mode");
            break;
          }
        }

        delay(10);
      }

      configurationServer.stop();
      return configurationScreen.nextRefreshInSeconds();
    }
    case CURRENT_WEATHER_SCREEN: {
      WeatherForecast forecastData = openMeteoAPI.getForecast(appConfig->latitude, appConfig->longitude);
      CurrentWeatherScreen currentWeatherScreen(display, forecastData, String(appConfig->city),
                                                String(appConfig->countryCode));
      currentWeatherScreen.render();
      return currentWeatherScreen.nextRefreshInSeconds();
    }
    case METEOGRAM_SCREEN: {
      WeatherForecast forecastData = openMeteoAPI.getForecast(appConfig->latitude, appConfig->longitude);
      MeteogramWeatherScreen meteogramWeatherScreen(display, forecastData);
      meteogramWeatherScreen.render();
      return meteogramWeatherScreen.nextRefreshInSeconds();
    }
    case MESSAGE_SCREEN: {
      WeatherForecast forecastData = openMeteoAPI.getForecast(appConfig->latitude, appConfig->longitude);

      String prompt = aiWeatherPrompt;
      prompt += "- Use the following style: " + String(appConfig->aiPromptStyle) + "\n";
      prompt += forecastData.apiPayload;

      ChatGPTClient chatGPTClient(appConfig->openaiApiKey);
      String chatGPTResponse = chatGPTClient.generateContent(prompt);

      MessageScreen messageScreen(display);
      messageScreen.setMessageText(chatGPTResponse);
      messageScreen.render();
      return messageScreen.nextRefreshInSeconds();
    }
    case IMAGE_SCREEN: {
      ImageScreen imageScreen(display, *appConfig);
      imageScreen.render();
      return imageScreen.nextRefreshInSeconds();
    }
    default: {
      Serial.println("Unknown screen index, defaulting to current weather");
      appConfig->currentScreenIndex = CURRENT_WEATHER_SCREEN;

      WeatherForecast forecastData = openMeteoAPI.getForecast(appConfig->latitude, appConfig->longitude);
      CurrentWeatherScreen currentWeatherScreen(display, forecastData, String(appConfig->city),
                                                String(appConfig->countryCode));
      currentWeatherScreen.render();
      return currentWeatherScreen.nextRefreshInSeconds();
    }
  }
}

void updateConfiguration(const Configuration& config) {
  if (config.ssid.length() >= sizeof(appConfig->wifiSSID)) {
    Serial.println("Error: SSID too long, maximum length is " + String(sizeof(appConfig->wifiSSID) - 1));
    return;
  }

  if (config.password.length() >= sizeof(appConfig->wifiPassword)) {
    Serial.println("Error: Password too long, maximum length is " + String(sizeof(appConfig->wifiPassword) - 1));
    return;
  }

  if (config.openaiApiKey.length() >= sizeof(appConfig->openaiApiKey)) {
    Serial.println("Error: OpenAI API key too long, maximum length is " + String(sizeof(appConfig->openaiApiKey) - 1));
    return;
  }

  if (config.aiPromptStyle.length() >= sizeof(appConfig->aiPromptStyle)) {
    Serial.println("Error: AI Prompt Style too long, maximum length is " +
                   String(sizeof(appConfig->aiPromptStyle) - 1));
    return;
  }

  if (config.city.length() >= sizeof(appConfig->city)) {
    Serial.println("Error: City too long, maximum length is " + String(sizeof(appConfig->city) - 1));
    return;
  }

  if (config.countryCode.length() >= sizeof(appConfig->countryCode)) {
    Serial.println("Error: Country code too long, maximum length is " + String(sizeof(appConfig->countryCode) - 1));
    return;
  }

  if (config.imageUrl.length() >= sizeof(appConfig->imageUrl)) {
    Serial.println("Error: Image URL too long, maximum length is " + String(sizeof(appConfig->imageUrl) - 1));
    return;
  }

  bool locationChanged =
      (config.city != String(appConfig->city)) || (config.countryCode != String(appConfig->countryCode));

  memset(appConfig->wifiSSID, 0, sizeof(appConfig->wifiSSID));
  memset(appConfig->wifiPassword, 0, sizeof(appConfig->wifiPassword));
  memset(appConfig->openaiApiKey, 0, sizeof(appConfig->openaiApiKey));
  memset(appConfig->aiPromptStyle, 0, sizeof(appConfig->aiPromptStyle));
  memset(appConfig->city, 0, sizeof(appConfig->city));
  memset(appConfig->countryCode, 0, sizeof(appConfig->countryCode));
  memset(appConfig->imageUrl, 0, sizeof(appConfig->imageUrl));

  strncpy(appConfig->wifiSSID, config.ssid.c_str(), sizeof(appConfig->wifiSSID) - 1);
  strncpy(appConfig->wifiPassword, config.password.c_str(), sizeof(appConfig->wifiPassword) - 1);
  strncpy(appConfig->openaiApiKey, config.openaiApiKey.c_str(), sizeof(appConfig->openaiApiKey) - 1);
  strncpy(appConfig->aiPromptStyle, config.aiPromptStyle.c_str(), sizeof(appConfig->aiPromptStyle) - 1);
  strncpy(appConfig->city, config.city.c_str(), sizeof(appConfig->city) - 1);
  strncpy(appConfig->countryCode, config.countryCode.c_str(), sizeof(appConfig->countryCode) - 1);
  strncpy(appConfig->imageUrl, config.imageUrl.c_str(), sizeof(appConfig->imageUrl) - 1);

  if (locationChanged) {
    appConfig->latitude = NAN;
    appConfig->longitude = NAN;
    Serial.println("Location changed - coordinates will be re-geocoded on next startup");
  }

  // NVS disabled - configuration not saved
  Serial.println("Configuration updated (not saved - NVS disabled)");

  Serial.println("Configuration updated");
  Serial.println("WiFi SSID: " + String(appConfig->wifiSSID));
  Serial.println("OpenAI API Key: " + String(appConfig->hasValidOpenaiApiKey() ? "[CONFIGURED]" : "[NOT SET]"));
  Serial.println("AI Prompt Style: " +
                 String(strlen(appConfig->aiPromptStyle) > 0 ? appConfig->aiPromptStyle : "[NOT SET]"));
  Serial.println("City: " + String(strlen(appConfig->city) > 0 ? appConfig->city : "[NOT SET]"));
  Serial.println("Country Code: " + String(strlen(appConfig->countryCode) > 0 ? appConfig->countryCode : "[NOT SET]"));
  Serial.println("Image URL: " + String(strlen(appConfig->imageUrl) > 0 ? appConfig->imageUrl : "[NOT SET]"));
}

void goToSleep(uint64_t sleepTimeInSeconds) {
  Serial.println("Going to deep sleep for " + String(sleepTimeInSeconds) + " seconds");
  Serial.println("Timer-only wakeup (button wakeup disabled for testing)");

  uint64_t sleepTimeMicros = sleepTimeInSeconds * 1000000ULL;
  // Temporarily disable button wakeup to test if it's causing premature wakeups
  // esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_1, 0);
  esp_sleep_enable_timer_wakeup(sleepTimeMicros);
  esp_deep_sleep_start();
}

void initializeDefaultConfig() {
  // NVS disabled - always use default configuration
  appConfig.reset(new ApplicationConfig());
  Serial.println("Using default configuration (NVS disabled)");
}

void setup() {
  Serial.begin(115200);

  initializeDefaultConfig();

  pinMode(BATTERY_PIN, INPUT);
  pinMode(BUTTON_1, INPUT_PULLUP);

  // Initialize SPI for display
  SPI.begin(EPD_SCLK, EPD_MISO, EPD_MOSI, EPD_CS);

#ifdef _HAS_LED_
  // Initialize and test LED
  pinMode(LED_PIN, OUTPUT);
  Serial.printf("LED pin initialized on GPIO %d\n", LED_PIN);
  Serial.printf("LED_ON value: %d\n", LED_ON);

  // Test LED - blink it a few times
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, LED_ON);  // Turn on
    Serial.println("LED should be ON");
    delay(200);
    digitalWrite(LED_PIN, !LED_ON);  // Turn off
    Serial.println("LED should be OFF");
    delay(200);
  }
#else
  Serial.println("_HAS_LED_ not defined - LED functionality disabled");
#endif

  // Check button state at startup
  Serial.printf("Button pin state at startup: %d\n", digitalRead(BUTTON_1));

  if (!isButtonWakeup()) {
    if (!appConfig->hasValidWiFiCredentials()) {
      appConfig->currentScreenIndex = CONFIG_SCREEN;
    }
  }

  // if (isButtonWakeup()) {
  //   cycleToNextScreen();
  // }

  if (appConfig->currentScreenIndex != CONFIG_SCREEN) {
    WiFiConnection wifi(appConfig->wifiSSID, appConfig->wifiPassword);
    wifi.connect();
    if (!wifi.isConnected()) {
      Serial.println("Failed to connect to WiFi");
      WifiErrorScreen errorScreen(display);
      errorScreen.render();
      int refreshSeconds = errorScreen.nextRefreshInSeconds();
      goToSleep(refreshSeconds);
      return;
    }

    if (isnan(appConfig->latitude) || isnan(appConfig->longitude)) {
      geocodeCurrentLocation();
      // NVS disabled - coordinates not saved
    }
  }

  int refreshSeconds = displayCurrentScreen();
  goToSleep(refreshSeconds);
}

void loop() {}
