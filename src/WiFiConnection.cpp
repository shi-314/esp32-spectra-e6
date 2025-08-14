#include "WiFiConnection.h"

#include <WiFi.h>
#include <esp_task_wdt.h>

#include "boards.h"

WiFiConnection::WiFiConnection(const char* ssid, const char* password)
    : _ssid(ssid), _password(password), connected(false) {}

void WiFiConnection::connect() {
  Serial.printf("Connecting to WiFi: %s\n", _ssid);
  
  // Simple WiFi initialization
  WiFi.mode(WIFI_STA);
  WiFi.begin(_ssid, _password);
  
  int attempts = 0;
  const int maxAttempts = 20;
  while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
    delay(500);
    Serial.printf("WiFi status: %d, attempt: %d/%d\n", WiFi.status(), attempts + 1, maxAttempts);
    attempts++;
    yield();  // Keep system responsive
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    connected = true;
  } else {
    Serial.println("\nFailed to connect to WiFi");
    connected = false;
  }
}

void WiFiConnection::reconnect() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Attempting to reconnect to WiFi...");
    WiFi.disconnect();
    delay(1000);
    connect();
  }
}

bool WiFiConnection::isConnected() { return WiFi.status() == WL_CONNECTED; }

void WiFiConnection::checkConnection() {
  if (WiFi.status() != WL_CONNECTED && connected) {
    Serial.println("WiFi connection lost");
    connected = false;
    reconnect();
  } else if (WiFi.status() == WL_CONNECTED && !connected) {
    Serial.println("WiFi reconnected");
    connected = true;
  }
}
