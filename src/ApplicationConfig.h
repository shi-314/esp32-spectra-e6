#pragma once

#include <Arduino.h>

#if __has_include("config_dev.h")
#include "config_dev.h"
// #include "config_default.h"
#else
#include "config_default.h"
#endif

enum ScreenType { CONFIG_SCREEN = 0, IMAGE_SCREEN = 1, SCREEN_COUNT = 2 };

struct ApplicationConfig {
  char wifiSSID[64];
  char wifiPassword[64];
  char imageUrl[300];
  int currentScreenIndex;

  ApplicationConfig() {
    memset(wifiSSID, 0, sizeof(wifiSSID));
    memset(wifiPassword, 0, sizeof(wifiPassword));
    memset(imageUrl, 0, sizeof(imageUrl));

    strncpy(wifiSSID, DEFAULT_WIFI_SSID, sizeof(wifiSSID) - 1);
    strncpy(wifiPassword, DEFAULT_WIFI_PASSWORD, sizeof(wifiPassword) - 1);
    strncpy(imageUrl, DEFAULT_IMAGE_URL, sizeof(imageUrl) - 1);

    currentScreenIndex = IMAGE_SCREEN;
  }

  bool hasValidWiFiCredentials() const { return strlen(wifiSSID) > 0 && strlen(wifiPassword) > 0; }
};
