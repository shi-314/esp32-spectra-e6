#pragma once

#include <Arduino.h>

#include "boards.h"

#if __has_include("config_dev.h")
#include "config_dev.h"
#else
#include "config_default.h"
#endif

enum ScreenType { CONFIG_SCREEN = 0, IMAGE_SCREEN = 1, METEOGRAM_SCREEN = 2, SCREEN_COUNT = 3 };

// Screens the buttons cycle through, in order. CONFIG_SCREEN is shown on demand, not cycled to.
static const ScreenType SELECTABLE_SCREENS[] = {IMAGE_SCREEN, METEOGRAM_SCREEN};
static const int SELECTABLE_SCREEN_COUNT = sizeof(SELECTABLE_SCREENS) / sizeof(SELECTABLE_SCREENS[0]);

struct ApplicationConfig {
  char wifiSSID[64];
  char wifiPassword[64];
  char imageUrl[300];
  char city[100];
  char countryCode[3];
  float latitude;
  float longitude;
  int currentScreenIndex;

  static const int DISPLAY_ROTATION = BOARD_DISPLAY_ROTATION;

  ApplicationConfig() {
    memset(wifiSSID, 0, sizeof(wifiSSID));
    memset(wifiPassword, 0, sizeof(wifiPassword));
    memset(imageUrl, 0, sizeof(imageUrl));
    memset(city, 0, sizeof(city));
    memset(countryCode, 0, sizeof(countryCode));

    strncpy(wifiSSID, DEFAULT_WIFI_SSID, sizeof(wifiSSID) - 1);
    strncpy(wifiPassword, DEFAULT_WIFI_PASSWORD, sizeof(wifiPassword) - 1);
    strncpy(imageUrl, DEFAULT_IMAGE_URL, sizeof(imageUrl) - 1);
    strncpy(city, DEFAULT_CITY, sizeof(city) - 1);
    strncpy(countryCode, DEFAULT_COUNTRY_CODE, sizeof(countryCode) - 1);

    // Coordinates are resolved from the city on first use
    latitude = NAN;
    longitude = NAN;

    currentScreenIndex = METEOGRAM_SCREEN;
  }

  bool hasValidCoordinates() const { return !isnan(latitude) && !isnan(longitude); }

  bool hasValidWiFiCredentials() const { return strlen(wifiSSID) > 0 && strlen(wifiPassword) > 0; }
};
