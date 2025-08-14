#ifndef CHATGPT_CLIENT_H
#define CHATGPT_CLIENT_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

class ChatGPTClient {
 private:
  const char* apiKey;
  const char* baseUrl;
  String model;
  WiFiClientSecure client;
  HTTPClient http;

  String makeRequest(const String& endpoint, const String& payload);

 public:
  ChatGPTClient(const char* apiKey);
  ~ChatGPTClient();

  void setModel(const String& modelName);
  String generateContent(const String& prompt);
};

#endif