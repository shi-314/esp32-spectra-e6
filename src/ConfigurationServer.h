#ifndef CONFIGURATION_SERVER_H
#define CONFIGURATION_SERVER_H

#include <Arduino.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>

#include <functional>

struct Configuration {
  String ssid;
  String password;
  String imageUrl;

  Configuration() = default;

  Configuration(const String &ssid, const String &password, const String &imageUrl)
      : ssid(ssid), password(password), imageUrl(imageUrl) {}
};

using OnSaveCallback = std::function<void(const Configuration &config)>;

class ConfigurationServer {
 public:
  static const char *WIFI_AP_NAME;
  static const char *WIFI_AP_PASSWORD;

  ConfigurationServer(const Configuration &currentConfig);
  void run(OnSaveCallback onSaveCallback);
  void stop();
  bool isRunning() const;
  void handleRequests();

  String getWifiAccessPointName() const;
  String getWifiAccessPointPassword() const;

 private:
  String deviceName;
  String wifiAccessPointName;
  String wifiAccessPointPassword;

  Configuration currentConfiguration;

  AsyncWebServer *server;
  DNSServer *dnsServer;
  bool isServerRunning;

  String htmlTemplate;
  OnSaveCallback onSaveCallback;

  void setupWebServer();
  void setupDNSServer();
  String getConfigurationPage();
  bool loadHtmlTemplate();
  void handleRoot(AsyncWebServerRequest *request);
  void handleSave(AsyncWebServerRequest *request);
  void handleNotFound(AsyncWebServerRequest *request);
};

#endif
