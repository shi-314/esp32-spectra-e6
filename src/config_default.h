#ifndef CONFIG_DEFAULT_H
#define CONFIG_DEFAULT_H

// Default configuration values (safe to commit to repository)
// For development, create config_dev.h with your actual credentials

const char DEFAULT_WIFI_SSID[] = "";
const char DEFAULT_WIFI_PASSWORD[] = "";
const char DEFAULT_IMAGE_URL[] = "";

// Location of the weather forecast, geocoded to coordinates on first use
const char DEFAULT_CITY[] = "Berlin";
const char DEFAULT_COUNTRY_CODE[] = "DE";

#endif  // CONFIG_DEFAULT_H