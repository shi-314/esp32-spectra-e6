#include "WiFiConnection.h"

#include <WiFi.h>

#include "boards.h"

WiFiConnection::WiFiConnection(const char* ssid, const char* password)
    : _ssid(ssid), _password(password), connected(false) {
#ifdef _HAS_LED_
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, !LED_ON);  // Turn LED off initially
#endif
}

void WiFiConnection::connect() {
  Serial.printf("Connecting to WiFi: %s\n", _ssid);
  WiFi.begin(_ssid, _password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    connected = true;
#ifdef _HAS_LED_
    digitalWrite(LED_PIN, LED_ON);  // Turn LED on when connected
    Serial.println("LED turned on (WiFi connected)");
#endif
  } else {
    Serial.println("\nFailed to connect to WiFi");
    connected = false;
#ifdef _HAS_LED_
    digitalWrite(LED_PIN, !LED_ON);  // Turn LED off when not connected
#endif
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
#ifdef _HAS_LED_
    digitalWrite(LED_PIN, !LED_ON);  // Turn LED off when connection lost
#endif
    reconnect();
  } else if (WiFi.status() == WL_CONNECTED && !connected) {
    Serial.println("WiFi reconnected");
    connected = true;
#ifdef _HAS_LED_
    digitalWrite(LED_PIN, LED_ON);  // Turn LED on when reconnected
#endif
  }
}
