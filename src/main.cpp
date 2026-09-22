#include <Arduino.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <time.h>

#include <memory>

#include "ApplicationConfig.h"
#include "ApplicationConfigStorage.h"
#include "ConfigurationScreen.h"
#include "ConfigurationServer.h"
#include "DisplayType.h"
#include "ImageScreen.h"
#include "WiFiConnection.h"
#include "battery.h"
#include "boards.h"
#include "esp32-hal.h"

std::unique_ptr<ApplicationConfig> appConfig;
ApplicationConfigStorage configStorage;

// Standard constructor for GxEPD2
DisplayType display(Epd2Type(EPD_CS, EPD_DC, EPD_RSET, EPD_BUSY));

#ifdef EPD_SPI_HOST
SPIClass epdSpi(EPD_SPI_HOST);
#else
SPIClass& epdSpi = SPI;
#endif

void goToSleep(uint64_t sleepTimeInSeconds);
int displayCurrentScreen(bool wifiConnected);
bool isButtonWakeup();
void updateConfiguration(const Configuration& config);
void initializeDefaultConfig();

bool isButtonWakeup() {
  esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();
  Serial.printf("Wakeup cause: %d (EXT0=%d, TIMER=%d)\n", wakeupReason, ESP_SLEEP_WAKEUP_EXT0, ESP_SLEEP_WAKEUP_TIMER);
  return (wakeupReason == ESP_SLEEP_WAKEUP_EXT0);
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
  } else {
    ImageScreen imageScreen(display, *appConfig);
    imageScreen.render();
    return imageScreen.nextRefreshInSeconds();
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

  Serial.println("Rebooting device to apply new configuration...");
  delay(1000);
  ESP.restart();
}

void goToSleep(uint64_t sleepTimeInSeconds) {
  Serial.println("Going to deep sleep for " + String(sleepTimeInSeconds) + " seconds");

  uint64_t sleepTimeMicros = sleepTimeInSeconds * 1000000ULL;
  esp_sleep_enable_timer_wakeup(sleepTimeMicros);
#ifdef WAKE_BUTTON_PIN
  esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKE_BUTTON_PIN, 0);
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
  } else {
    appConfig.reset(new ApplicationConfig());
    Serial.println("Using default configuration (no stored config found)");
  }
}

void setup() {
  delay(1000);
  Serial.begin(115200);

  initializeDefaultConfig();

  if (isButtonWakeup()) {
    // Treat a button press like a restart: re-download and redraw even if the image is unchanged
    Serial.println("Woken by button, forcing image refresh");
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
