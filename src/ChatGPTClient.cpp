#include "ChatGPTClient.h"

const char* OPENAI_BASE_URL = "https://api.openai.com";

ChatGPTClient::ChatGPTClient(const char* apiKey) {
  this->apiKey = apiKey;
  baseUrl = OPENAI_BASE_URL;
  model = "gpt-4.1-mini";
  client.setInsecure();
}

ChatGPTClient::~ChatGPTClient() { http.end(); }

String ChatGPTClient::makeRequest(const String& endpoint, const String& payload) {
  String url = String(baseUrl) + endpoint;

  Serial.println("Request URL: " + url);
  Serial.println("Payload size: " + String(payload.length()) + " bytes");

  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(apiKey));

  http.setTimeout(30000);  // 30 second timeout

  int httpResponseCode = http.POST(payload);
  String response = "";

  if (httpResponseCode > 0) {
    response = http.getString();
    Serial.println("HTTP Response Code: " + String(httpResponseCode));
  } else {
    Serial.println("Error in HTTP request: " + String(httpResponseCode));
    Serial.println("Error details:");
    switch (httpResponseCode) {
      case -1:
        Serial.println("Connection failed");
        break;
      case -2:
        Serial.println("Send header failed");
        break;
      case -3:
        Serial.println("Send payload failed");
        break;
      case -4:
        Serial.println("Not connected");
        break;
      case -5:
        Serial.println("Connection lost");
        break;
      case -6:
        Serial.println("No stream");
        break;
      case -7:
        Serial.println("No HTTP server");
        break;
      case -8:
        Serial.println("Too less RAM");
        break;
      case -9:
        Serial.println("Encoding error");
        break;
      case -10:
        Serial.println("Stream write error");
        break;
      case -11:
        Serial.println("Read timeout");
        break;
      default:
        Serial.println("Unknown error");
    }
  }

  http.end();
  return response;
}

void ChatGPTClient::setModel(const String& modelName) { model = modelName; }

String ChatGPTClient::generateContent(const String& prompt) {
  String endpoint = "/v1/chat/completions";

  if (prompt.length() == 0) {
    Serial.println("Error: Empty prompt");
    return "Error: Empty prompt";
  }

  // Create OpenAI chat completions format
  DynamicJsonDocument doc(8192);
  doc["model"] = model;

  JsonArray messages = doc.createNestedArray("messages");
  JsonObject message = messages.createNestedObject();
  message["role"] = "user";
  message["content"] = prompt;

  doc["max_tokens"] = 1000;
  doc["temperature"] = 0.7;

  String payload;
  serializeJson(doc, payload);

  String response = makeRequest(endpoint, payload);

  DynamicJsonDocument responseDoc(8192);
  deserializeJson(responseDoc, response);

  if (responseDoc.containsKey("choices") && responseDoc["choices"].size() > 0 &&
      responseDoc["choices"][0].containsKey("message") && responseDoc["choices"][0]["message"].containsKey("content")) {
    return responseDoc["choices"][0]["message"]["content"].as<String>();
  }

  Serial.println("Error: Could not parse response");
  Serial.println(response);

  return "Error: Could not parse response";
}