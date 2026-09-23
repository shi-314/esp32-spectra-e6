#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <esp_task_wdt.h>
#include <time.h>

#include <memory>

#include "ApplicationConfig.h"
#include "ApplicationConfigStorage.h"
#include "ConfigurationScreen.h"
#include "ConfigurationServer.h"
#include "DisplayType.h"
#include "ImageScreen.h"
#include "MeteogramScreen.h"
#include "OpenMeteoAPI.h"
#include "WiFiConnection.h"
#include "battery.h"
#include "boards.h"
#include "esp32-hal.h"

std::unique_ptr<ApplicationConfig> appConfig;
ApplicationConfigStorage configStorage;
OpenMeteoAPI openMeteoAPI;

// Standard constructor for GxEPD2
DisplayType display(Epd2Type(EPD_CS, EPD_DC, EPD_RSET, EPD_BUSY));

#ifdef EPD_SPI_HOST
SPIClass epdSpi(EPD_SPI_HOST);
#else
SPIClass& epdSpi = SPI;
#endif

enum ButtonWakeup { NO_BUTTON, REFRESH_BUTTON, NEXT_SCREEN_BUTTON, PREV_SCREEN_BUTTON };

void goToSleep(uint64_t sleepTimeInSeconds);
int displayCurrentScreen(bool wifiConnected);
ButtonWakeup getButtonWakeup();
void cycleScreen(int direction);
void geocodeCurrentLocation();
void updateConfiguration(const Configuration& config);
void initializeDefaultConfig();

ButtonWakeup getButtonWakeup() {
  esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();
  Serial.printf("Wakeup cause: %d (EXT1=%d, TIMER=%d)\n", wakeupReason, ESP_SLEEP_WAKEUP_EXT1, ESP_SLEEP_WAKEUP_TIMER);

#ifdef WAKE_BUTTON_PIN
  if (wakeupReason == ESP_SLEEP_WAKEUP_EXT1) {
    uint64_t wakeupPins = esp_sleep_get_ext1_wakeup_status();
    Serial.printf("Wakeup pins: 0x%llx\n", wakeupPins);

#ifdef NEXT_SCREEN_BUTTON_PIN
    if (wakeupPins & (1ULL << NEXT_SCREEN_BUTTON_PIN)) return NEXT_SCREEN_BUTTON;
#endif
#ifdef PREV_SCREEN_BUTTON_PIN
    if (wakeupPins & (1ULL << PREV_SCREEN_BUTTON_PIN)) return PREV_SCREEN_BUTTON;
#endif
    if (wakeupPins & (1ULL << WAKE_BUTTON_PIN)) return REFRESH_BUTTON;
  }
#endif

  return NO_BUTTON;
}

void cycleScreen(int direction) {
  int currentIndex = 0;
  for (int i = 0; i < SELECTABLE_SCREEN_COUNT; i++) {
    if (SELECTABLE_SCREENS[i] == appConfig->currentScreenIndex) {
      currentIndex = i;
      break;
    }
  }

  int nextIndex = (currentIndex + direction + SELECTABLE_SCREEN_COUNT) % SELECTABLE_SCREEN_COUNT;
  appConfig->currentScreenIndex = SELECTABLE_SCREENS[nextIndex];
  Serial.printf("Switched to screen %d\n", appConfig->currentScreenIndex);

  configStorage.save(*appConfig);
}

void geocodeCurrentLocation() {
  if (strlen(appConfig->city) == 0) {
    Serial.println("No city configured, cannot resolve coordinates");
    return;
  }

  Serial.printf("Geocoding location: %s (%s)\n", appConfig->city, appConfig->countryCode);

  GeocodingResult location = openMeteoAPI.getLocationByCity(String(appConfig->city), String(appConfig->countryCode));
  if (location.name.length() == 0) {
    Serial.printf("Geocoding failed for %s\n", appConfig->city);
    return;
  }

  appConfig->latitude = location.latitude;
  appConfig->longitude = location.longitude;
  strncpy(appConfig->city, location.name.c_str(), sizeof(appConfig->city) - 1);
  strncpy(appConfig->countryCode, location.countryCode.c_str(), sizeof(appConfig->countryCode) - 1);

  Serial.printf("Geocoded %s -> (%f, %f)\n", appConfig->city, appConfig->latitude, appConfig->longitude);
  configStorage.save(*appConfig);
}

int displayCurrentScreen(bool wifiConnected) {
  if (!appConfig->hasValidWiFiCredentials() || !wifiConnected) {
    if (!appConfig->hasValidWiFiCredentials()) {
      Serial.println("No valid WiFi credentials, showing configuration screen");
    } else {
      Serial.println("Failed to connect to WiFi, showing configuration screen");
    }

    ConfigurationScreen configurationScreen(display);
    configurationScreen.render();

    Configuration currentConfig = Configuration(appConfig->wifiSSID, appConfig->wifiPassword, appConfig->imageUrl);
    ConfigurationServer configurationServer(currentConfig);

    configurationServer.run(updateConfiguration);

    while (configurationServer.isRunning()) {
      configurationServer.handleRequests();
      delay(10);
    }

    configurationServer.stop();
    return configurationScreen.nextRefreshInSeconds();
  }

  if (appConfig->currentScreenIndex == METEOGRAM_SCREEN) {
    if (!appConfig->hasValidCoordinates()) {
      geocodeCurrentLocation();
    }

    WeatherForecast forecast = openMeteoAPI.getForecast(appConfig->latitude, appConfig->longitude);
    MeteogramScreen meteogramScreen(display, forecast, String(appConfig->city));
    meteogramScreen.render();
    return meteogramScreen.nextRefreshInSeconds();
  }

  ImageScreen imageScreen(display, *appConfig);
  imageScreen.render();
  return imageScreen.nextRefreshInSeconds();
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

  if (config.imageUrl.length() >= sizeof(appConfig->imageUrl)) {
    Serial.println("Error: Image URL too long, maximum length is " + String(sizeof(appConfig->imageUrl) - 1));
    return;
  }

  memset(appConfig->wifiSSID, 0, sizeof(appConfig->wifiSSID));
  memset(appConfig->wifiPassword, 0, sizeof(appConfig->wifiPassword));
  memset(appConfig->imageUrl, 0, sizeof(appConfig->imageUrl));

  strncpy(appConfig->wifiSSID, config.ssid.c_str(), sizeof(appConfig->wifiSSID) - 1);
  strncpy(appConfig->wifiPassword, config.password.c_str(), sizeof(appConfig->wifiPassword) - 1);
  strncpy(appConfig->imageUrl, config.imageUrl.c_str(), sizeof(appConfig->imageUrl) - 1);

  // Save configuration to persistent storage
  bool saved = configStorage.save(*appConfig);
  if (saved) {
    Serial.println("Configuration saved to persistent storage");
  } else {
    Serial.println("Failed to save configuration to persistent storage");
  }

  Serial.println("Configuration updated");
  Serial.println("WiFi SSID: " + String(appConfig->wifiSSID));
  Serial.println("Image URL: " + String(strlen(appConfig->imageUrl) > 0 ? appConfig->imageUrl : "[NOT SET]"));

  Serial.println("Rebooting device to apply new configuration...");
  delay(1000);
  ESP.restart();
}

void goToSleep(uint64_t sleepTimeInSeconds) {
  Serial.println("Going to deep sleep for " + String(sleepTimeInSeconds) + " seconds");

  uint64_t sleepTimeMicros = sleepTimeInSeconds * 1000000ULL;
  esp_sleep_enable_timer_wakeup(sleepTimeMicros);
#ifdef WAKE_BUTTON_PIN
  uint64_t buttonMask = 1ULL << WAKE_BUTTON_PIN;
#ifdef NEXT_SCREEN_BUTTON_PIN
  buttonMask |= 1ULL << NEXT_SCREEN_BUTTON_PIN;
#endif
#ifdef PREV_SCREEN_BUTTON_PIN
  buttonMask |= 1ULL << PREV_SCREEN_BUTTON_PIN;
#endif
  // On the ESP32-S3 this mode wakes on ANY selected pin going low (renamed ESP_EXT1_WAKEUP_ANY_LOW in newer IDF)
  esp_sleep_enable_ext1_wakeup(buttonMask, ESP_EXT1_WAKEUP_ALL_LOW);
#endif
  esp_deep_sleep_start();
}

void initializeDefaultConfig() {
  std::unique_ptr<ApplicationConfig> storedConfig = configStorage.load();
  if (storedConfig) {
    appConfig = std::move(storedConfig);
    Serial.println("Configuration loaded from persistent storage: ");
    Serial.printf("  - WiFi SSID: %s\n", appConfig->wifiSSID);
    Serial.printf("  - Image URL: %s\n", strlen(appConfig->imageUrl) > 0 ? appConfig->imageUrl : "[NOT SET]");
    Serial.printf("  - Location: %s (%s)\n", appConfig->city, appConfig->countryCode);
    Serial.printf("  - Screen: %d\n", appConfig->currentScreenIndex);
  } else {
    appConfig.reset(new ApplicationConfig());
    Serial.println("Using default configuration (no stored config found)");
  }
}

void setup() {
  delay(1000);
  Serial.begin(115200);

  initializeDefaultConfig();

  ButtonWakeup buttonWakeup = getButtonWakeup();
  if (buttonWakeup == NEXT_SCREEN_BUTTON) {
    cycleScreen(1);
  } else if (buttonWakeup == PREV_SCREEN_BUTTON) {
    cycleScreen(-1);
  }

  if (buttonWakeup != NO_BUTTON) {
    // Treat a button press like a restart: re-download and redraw even if the image is unchanged
    Serial.println("Woken by button, forcing refresh");
    ImageScreen::clearStoredImageETag();
  }

  pinMode(BATTERY_PIN, INPUT);

#ifdef SD_POWER_PIN
  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);
  pinMode(SD_POWER_PIN, OUTPUT);
  digitalWrite(SD_POWER_PIN, HIGH);
#endif

  epdSpi.begin(EPD_SCLK, EPD_MISO, EPD_MOSI, EPD_CS);
#ifdef EPD_SPI_FREQUENCY
  display.epd2.selectSPI(epdSpi, SPISettings(EPD_SPI_FREQUENCY, MSBFIRST, SPI_MODE0));
#endif

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_ON);

  // Try to connect to WiFi if we have valid credentials
  WiFiConnection wifi(appConfig->wifiSSID, appConfig->wifiPassword);
  if (appConfig->hasValidWiFiCredentials()) {
    Serial.printf("WiFi credentials loaded: SSID='%s', Password length=%d\n", appConfig->wifiSSID,
                  strlen(appConfig->wifiPassword));
    wifi.connect();
  }

  int refreshSeconds = displayCurrentScreen(wifi.isConnected());
  goToSleep(refreshSeconds);
}

void loop() {}
