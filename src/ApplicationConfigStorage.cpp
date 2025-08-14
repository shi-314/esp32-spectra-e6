#include "ApplicationConfigStorage.h"

ApplicationConfigStorage::ApplicationConfigStorage() {
  // NVS disabled - no initialization needed
}

ApplicationConfigStorage::~ApplicationConfigStorage() {}

bool ApplicationConfigStorage::save(const ApplicationConfig& config) {
  // NVS disabled - configuration saving disabled
  Serial.println("Configuration saving disabled (NVS disabled)");
  return true;
}

std::unique_ptr<ApplicationConfig> ApplicationConfigStorage::load() {
  // NVS disabled - always return nullptr (no stored config)
  Serial.println("Configuration loading disabled (NVS disabled)");
  return nullptr;
}

void ApplicationConfigStorage::clear() {
  // NVS disabled - no configuration to clear
  Serial.println("Configuration clearing disabled (NVS disabled)");
}