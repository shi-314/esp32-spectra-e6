#include "ConfigurationServer.h"

#include <SPIFFS.h>
#include <WiFi.h>
#include <WiFiAP.h>

const char *ConfigurationServer::WIFI_AP_NAME = "Framey-Config";
const char *ConfigurationServer::WIFI_AP_PASSWORD = "configure123";

ConfigurationServer::ConfigurationServer(const Configuration &currentConfig)
    : deviceName("LilyGo-Weather-Station"),
      wifiAccessPointName(WIFI_AP_NAME),
      wifiAccessPointPassword(WIFI_AP_PASSWORD),
      currentConfiguration(currentConfig),
      server(nullptr),
      dnsServer(nullptr),
      isServerRunning(false) {}

void ConfigurationServer::run(OnSaveCallback onSaveCallback) {
  this->onSaveCallback = onSaveCallback;

  Serial.println("Starting Configuration Server...");
  Serial.print("Device Name: ");
  Serial.println(deviceName);

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed - filesystem must be uploaded first");
    return;
  }
  Serial.println("SPIFFS initialized successfully");

  if (!loadHtmlTemplate()) {
    Serial.println("Failed to load HTML template");
    SPIFFS.end();
    return;
  }
  Serial.println("HTML template loaded successfully");
  SPIFFS.end();

  WiFi.disconnect(true);
  delay(1000);

  Serial.print("Setting up WiFi Access Point: ");
  Serial.println(wifiAccessPointName);

  WiFi.mode(WIFI_AP);
  bool apStarted = WiFi.softAP(wifiAccessPointName.c_str(), wifiAccessPointPassword.c_str());

  if (apStarted) {
    Serial.println("Access Point started successfully!");
    Serial.print("Network Name (SSID): ");
    Serial.println(wifiAccessPointName);
    Serial.print("Password: ");
    Serial.println(wifiAccessPointPassword);
    Serial.print("Access Point IP: ");
    Serial.println(WiFi.softAPIP());
    Serial.println("Setting up captive portal...");

    setupDNSServer();
    setupWebServer();

    isServerRunning = true;
    Serial.println("Captive portal is running!");
    Serial.println("Devices connecting to this network will be automatically redirected to the configuration page");
  } else {
    Serial.println("Failed to start Access Point!");
  }
}

void ConfigurationServer::stop() {
  if (isServerRunning) {
    if (server) {
      delete server;
      server = nullptr;
    }
    if (dnsServer) {
      dnsServer->stop();
      delete dnsServer;
      dnsServer = nullptr;
    }
    WiFi.softAPdisconnect(true);
    isServerRunning = false;
    Serial.println("Configuration server stopped");
  }
}

void ConfigurationServer::handleRequests() {
  if (isServerRunning && dnsServer) {
    dnsServer->processNextRequest();
  }
}

void ConfigurationServer::setupDNSServer() {
  dnsServer = new DNSServer();
  const byte DNS_PORT = 53;
  dnsServer->start(DNS_PORT, "*", WiFi.softAPIP());
  Serial.println("DNS Server started - all domains redirect to captive portal");
}

void ConfigurationServer::setupWebServer() {
  server = new AsyncWebServer(80);

  server->on("/generate_204", HTTP_GET, [this](AsyncWebServerRequest *request) { handleRoot(request); });  // Android
  server->on("/fwlink", HTTP_GET, [this](AsyncWebServerRequest *request) { handleRoot(request); });        // Microsoft
  server->on("/hotspot-detect.html", HTTP_GET, [this](AsyncWebServerRequest *request) { handleRoot(request); });  // iOS
  server->on("/connectivity-check.html", HTTP_GET,
             [this](AsyncWebServerRequest *request) { handleRoot(request); });  // Firefox

  server->on("/", HTTP_GET, [this](AsyncWebServerRequest *request) { handleRoot(request); });
  server->on("/config", HTTP_GET, [this](AsyncWebServerRequest *request) { handleRoot(request); });
  server->on("/save", HTTP_POST, [this](AsyncWebServerRequest *request) { handleSave(request); });

  server->onNotFound([this](AsyncWebServerRequest *request) { handleNotFound(request); });

  server->begin();
  Serial.println("Web server started on port 80");
}

void ConfigurationServer::handleRoot(AsyncWebServerRequest *request) {
  String html = getConfigurationPage();
  request->send(200, "text/html", html);
}

void ConfigurationServer::handleSave(AsyncWebServerRequest *request) {
  if (request->hasParam("ssid", true) && request->hasParam("password", true)) {
    Configuration config;
    config.ssid = request->getParam("ssid", true)->value();
    config.password = request->getParam("password", true)->value();

    if (request->hasParam("imageUrl", true)) {
      config.imageUrl = request->getParam("imageUrl", true)->value();
    }

    Serial.println("Configuration received");
    request->send(200, "text/plain", "OK");

    onSaveCallback(config);
  } else {
    request->send(400, "text/plain", "Missing parameters");
  }

  stop();
}

void ConfigurationServer::handleNotFound(AsyncWebServerRequest *request) { request->redirect("/"); }

bool ConfigurationServer::loadHtmlTemplate() {
  File file = SPIFFS.open("/config.html", "r");
  if (!file) {
    Serial.println("Failed to open config.html file");
    return false;
  }

  htmlTemplate = file.readString();
  file.close();

  if (htmlTemplate.length() == 0) {
    Serial.println("config.html file is empty");
    return false;
  }

  return true;
}

String ConfigurationServer::getConfigurationPage() {
  String html = htmlTemplate;
  html.replace("{{CURRENT_SSID}}", currentConfiguration.ssid);
  html.replace("{{CURRENT_PASSWORD}}", currentConfiguration.password);
  html.replace("{{CURRENT_IMAGE_URL}}", currentConfiguration.imageUrl);
  return html;
}

String ConfigurationServer::getWifiAccessPointName() const { return wifiAccessPointName; }

String ConfigurationServer::getWifiAccessPointPassword() const { return wifiAccessPointPassword; }

bool ConfigurationServer::isRunning() const { return isServerRunning; }
