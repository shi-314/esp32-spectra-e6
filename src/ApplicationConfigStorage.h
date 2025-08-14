#pragma once

#include <memory>

#include "ApplicationConfig.h"

class ApplicationConfigStorage {
 public:
  ApplicationConfigStorage();
  ~ApplicationConfigStorage();

  bool save(const ApplicationConfig& config);
  std::unique_ptr<ApplicationConfig> load();
  void clear();

 private:
  static const char* NVS_NAMESPACE;
  static const char* CONFIG_KEY;
};