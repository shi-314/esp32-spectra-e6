#include <Arduino.h>
#include <time.h>

#include <memory>

#include "ApplicationConfig.h"
#include "ApplicationConfigStorage.h"
#include "ConfigurationScreen.h"
#include "ConfigurationServer.h"
#include "DisplayType.h"
#include "ImageScreen.h"
#include "WiFiConnection.h"
#include "WifiErrorScreen.h"
#include "battery.h"
#include "boards.h"

std::unique_ptr<ApplicationConfig> appConfig;
ApplicationConfigStorage configStorage;


// Standard constructor for GxEPD2
DisplayType display(Epd2Type(EPD_CS, EPD_DC, EPD_RSET, EPD_BUSY));


void goToSleep(uint64_t sleepTimeInSeconds);
int displayCurrentScreen();
void cycleToNextScreen();
bool isButtonWakeup();
void updateConfiguration(const Configuration& config);
void initializeDefaultConfig();


bool isButtonWakeup() {
  esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();
  Serial.printf("Wakeup cause: %d (EXT0=%d, TIMER=%d)\n", wakeupReason, ESP_SLEEP_WAKEUP_EXT0, ESP_SLEEP_WAKEUP_TIMER);
  return (wakeupReason == ESP_SLEEP_WAKEUP_EXT0);
}

void cycleToNextScreen() {
  appConfig->currentScreenIndex = (appConfig->currentScreenIndex + 1) % SCREEN_COUNT;

  Serial.println("Cycled to screen: " + String(appConfig->currentScreenIndex));

  configStorage.save(*appConfig);
}

int displayCurrentScreen() {
  switch (appConfig->currentScreenIndex) {
    case CONFIG_SCREEN: {
      ConfigurationScreen configurationScreen(display);
      configurationScreen.render();

      Configuration currentConfig =
          Configuration(appConfig->wifiSSID, appConfig->wifiPassword, appConfig->imageUrl);
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
    case IMAGE_SCREEN: {
      ImageScreen imageScreen(display, *appConfig);
      imageScreen.render();
      return imageScreen.nextRefreshInSeconds();
    }
    default: {
      Serial.println("Unknown screen index, defaulting to image screen");
      appConfig->currentScreenIndex = IMAGE_SCREEN;

      ImageScreen imageScreen(display, *appConfig);
      imageScreen.render();
      return imageScreen.nextRefreshInSeconds();
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
  std::unique_ptr<ApplicationConfig> storedConfig = configStorage.load();
  if (storedConfig) {
    appConfig = std::move(storedConfig);
    Serial.println("Configuration loaded from persistent storage: ");
    Serial.printf("  - WiFi SSID: %s\n", appConfig->wifiSSID);
    Serial.printf("  - Image URL: %s\n", strlen(appConfig->imageUrl) > 0 ? appConfig->imageUrl : "[NOT SET]");
  } else {
    appConfig.reset(new ApplicationConfig());
    Serial.println("Using default configuration (no stored config found)");
  }
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
  }

  int refreshSeconds = displayCurrentScreen();
  goToSleep(refreshSeconds);
}

void loop() {}
