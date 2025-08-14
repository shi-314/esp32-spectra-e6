#include "ApplicationConfigStorage.h"

#include <nvs.h>
#include <nvs_flash.h>

const char* ApplicationConfigStorage::NVS_NAMESPACE = "weather_config";
const char* ApplicationConfigStorage::CONFIG_KEY = "app_config";

ApplicationConfigStorage::ApplicationConfigStorage() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
}

ApplicationConfigStorage::~ApplicationConfigStorage() {}

bool ApplicationConfigStorage::save(const ApplicationConfig& config) {
  nvs_handle_t nvsHandle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvsHandle);
  if (err != ESP_OK) {
    Serial.printf("Error opening NVS handle: %s\n", esp_err_to_name(err));
    return false;
  }

  err = nvs_set_blob(nvsHandle, CONFIG_KEY, &config, sizeof(ApplicationConfig));
  if (err != ESP_OK) {
    Serial.printf("Error saving config to NVS: %s\n", esp_err_to_name(err));
    nvs_close(nvsHandle);
    return false;
  }

  err = nvs_commit(nvsHandle);
  if (err != ESP_OK) {
    Serial.printf("Error committing to NVS: %s\n", esp_err_to_name(err));
    nvs_close(nvsHandle);
    return false;
  }

  nvs_close(nvsHandle);
  Serial.println("Configuration saved to NVS successfully");
  return true;
}

std::unique_ptr<ApplicationConfig> ApplicationConfigStorage::load() {
  nvs_handle_t nvsHandle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvsHandle);
  if (err != ESP_OK) {
    Serial.printf("Error opening NVS handle for reading: %s\n", esp_err_to_name(err));
    return nullptr;
  }

  size_t requiredSize = sizeof(ApplicationConfig);
  std::unique_ptr<ApplicationConfig> config(new ApplicationConfig());

  err = nvs_get_blob(nvsHandle, CONFIG_KEY, config.get(), &requiredSize);
  nvs_close(nvsHandle);

  if (err == ESP_ERR_NVS_NOT_FOUND) {
    Serial.println("No configuration found in NVS");
    return nullptr;
  } else if (err != ESP_OK) {
    Serial.printf("Error reading config from NVS: %s\n", esp_err_to_name(err));
    return nullptr;
  }

  if (requiredSize != sizeof(ApplicationConfig)) {
    Serial.println("Configuration size mismatch, ignoring stored config");
    return nullptr;
  }

  Serial.println("Configuration loaded from NVS successfully");
  return config;
}

void ApplicationConfigStorage::clear() {
  nvs_handle_t nvsHandle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvsHandle);
  if (err != ESP_OK) {
    Serial.printf("Error opening NVS handle: %s\n", esp_err_to_name(err));
    return;
  }

  err = nvs_erase_key(nvsHandle, CONFIG_KEY);
  if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
    Serial.printf("Error clearing config from NVS: %s\n", esp_err_to_name(err));
  } else {
    Serial.println("Configuration cleared from NVS");
  }

  nvs_commit(nvsHandle);
  nvs_close(nvsHandle);
}