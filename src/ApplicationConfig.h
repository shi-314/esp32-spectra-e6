#pragma once

#include <Arduino.h>

#if __has_include("config_dev.h")
#include "config_dev.h"
#else
#include "config_default.h"
#endif

enum ScreenType {
  CONFIG_SCREEN = 0,
  IMAGE_SCREEN = 1,
  SCREEN_COUNT = 2
};

struct ApplicationConfig {
  char wifiSSID[64];
  char wifiPassword[64];
  char openaiApiKey[200];
  char aiPromptStyle[200];
  char city[100];
  char countryCode[3];
  char imageUrl[300];
  float latitude;
  float longitude;
  int currentScreenIndex;

  ApplicationConfig() {
    memset(wifiSSID, 0, sizeof(wifiSSID));
    memset(wifiPassword, 0, sizeof(wifiPassword));
    memset(openaiApiKey, 0, sizeof(openaiApiKey));
    memset(aiPromptStyle, 0, sizeof(aiPromptStyle));
    memset(city, 0, sizeof(city));
    memset(countryCode, 0, sizeof(countryCode));
    memset(imageUrl, 0, sizeof(imageUrl));

    strncpy(wifiSSID, DEFAULT_WIFI_SSID, sizeof(wifiSSID) - 1);
    strncpy(wifiPassword, DEFAULT_WIFI_PASSWORD, sizeof(wifiPassword) - 1);
    strncpy(openaiApiKey, DEFAULT_OPENAI_API_KEY, sizeof(openaiApiKey) - 1);
    strncpy(aiPromptStyle, DEFAULT_AI_PROMPT_STYLE, sizeof(aiPromptStyle) - 1);
    strncpy(city, DEFAULT_CITY, sizeof(city) - 1);
    strncpy(countryCode, DEFAULT_COUNTRY_CODE, sizeof(countryCode) - 1);
    strncpy(imageUrl, DEFAULT_IMAGE_URL, sizeof(imageUrl) - 1);

    latitude = DEFAULT_LATITUDE;
    longitude = DEFAULT_LONGITUDE;

    currentScreenIndex = IMAGE_SCREEN;
  }

  bool hasValidWiFiCredentials() const { return strlen(wifiSSID) > 0 && strlen(wifiPassword) > 0; }

  bool hasValidOpenaiApiKey() const { return strlen(openaiApiKey) > 0; }
};