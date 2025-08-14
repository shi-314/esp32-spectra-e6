#include "HttpDownloader.h"

#include <WiFi.h>

HttpDownloader::HttpDownloader() {}

HttpDownloader::~HttpDownloader() {}

std::unique_ptr<DownloadResult> HttpDownloader::download(const String& url, const String& cachedETag) {
  HTTPClient http;
  auto result = std::unique_ptr<DownloadResult>(new DownloadResult());

  Serial.println("Requesting data from: " + url);

  http.begin(url);
  http.setTimeout(10000);

  if (cachedETag.length() > 0) {
    http.addHeader("If-None-Match", cachedETag);
  }

  const char* headerKeys[] = {"Content-Type", "Transfer-Encoding", "ETag"};
  size_t headerKeysSize = sizeof(headerKeys) / sizeof(char*);
  http.collectHeaders(headerKeys, headerKeysSize);

  int httpCode = http.GET();
  result->httpCode = httpCode;

  if (httpCode == HTTP_CODE_NOT_MODIFIED) {
    Serial.println("Content not modified (304), using cached version");
    http.end();
    return result;
  }

  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("HTTP request failed with code: %d\n", httpCode);
    Serial.printf("HTTP error: %s\n", http.errorToString(httpCode).c_str());
    http.end();
    return result;
  }

  String newETag = http.header("ETag");
  if (newETag.length() > 0) {
    result->etag = newETag;
  }

  String contentType = http.header("Content-Type");
  contentType.toLowerCase();
  if (!contentType.isEmpty() && contentType != "image/bmp") {
    Serial.println("Unexpected content type: " + contentType);
    http.end();
    result->httpCode = -1;
    return result;
  }

  String transferEncoding = http.header("Transfer-Encoding");
  bool isChunked = transferEncoding.indexOf("chunked") != -1;

  WiFiClient* stream = http.getStreamPtr();

  if (isChunked) {
    result = downloadChunked(stream);
  } else {
    result = downloadRegular(stream);
  }

  http.end();
  result->httpCode = httpCode;
  result->etag = newETag;

  if (result->size > 0) {
    Serial.printf("Downloaded %d bytes\n", result->size);
  }

  return result;
}

std::unique_ptr<DownloadResult> HttpDownloader::downloadChunked(WiFiClient* stream) {
  auto result = std::unique_ptr<DownloadResult>(new DownloadResult());

  size_t bufferCapacity = 400 * 1024;
  result->data = (uint8_t*)ps_malloc(bufferCapacity);
  if (!result->data) {
    Serial.println("Failed to allocate PSRAM buffer");
    result->httpCode = -1;
    return result;
  }

  result->size = 0;

  while (stream->connected()) {
    char chunkSizeBuffer[16];
    size_t lineLength = stream->readBytesUntil('\n', (uint8_t*)chunkSizeBuffer, sizeof(chunkSizeBuffer) - 1);
    chunkSizeBuffer[lineLength] = '\0';

    if (lineLength > 0 && chunkSizeBuffer[lineLength - 1] == '\r') {
      chunkSizeBuffer[lineLength - 1] = '\0';
      lineLength--;
    }

    if (lineLength == 0) {
      continue;
    }

    long chunkSize = strtol(chunkSizeBuffer, NULL, 16);

    if (chunkSize == 0) {
      break;
    }

    if (result->size + chunkSize > bufferCapacity) {
      bufferCapacity = max(bufferCapacity * 2, (size_t)(result->size + chunkSize + 1024));

      result->data = (uint8_t*)ps_realloc(result->data, bufferCapacity);
      if (!result->data) {
        Serial.println("Failed to expand PSRAM buffer");
        result->httpCode = -1;
        return result;
      }
    }

    size_t bytesRead = stream->readBytes(result->data + result->size, chunkSize);
    if (bytesRead != chunkSize) {
      Serial.printf("Warning: Expected %ld bytes, got %d bytes\n", chunkSize, bytesRead);
    }
    result->size += bytesRead;

    uint8_t trailer[2];
    stream->readBytes(trailer, 2);
  }

  if (result->size == 0) {
    Serial.println("No data received from chunked response");
    result->httpCode = -1;
    return result;
  }
  return result;
}

std::unique_ptr<DownloadResult> HttpDownloader::downloadRegular(WiFiClient* stream) {
  auto result = std::unique_ptr<DownloadResult>(new DownloadResult());

  size_t bufferCapacity = 400 * 1024;
  result->data = (uint8_t*)ps_malloc(bufferCapacity);
  if (!result->data) {
    Serial.println("Failed to allocate PSRAM buffer");
    result->httpCode = -1;
    return result;
  }

  result->size = 0;
  const size_t chunkSize = 1024;

  while (stream->available() > 0) {
    if (result->size + chunkSize > bufferCapacity) {
      bufferCapacity = bufferCapacity * 2;
      result->data = (uint8_t*)ps_realloc(result->data, bufferCapacity);
      if (!result->data) {
        result->httpCode = -1;
        return result;
      }
    }

    size_t bytesRead = stream->readBytes(result->data + result->size, chunkSize);
    if (bytesRead == 0) break;
    result->size += bytesRead;
  }
  return result;
}

String HttpDownloader::urlEncode(const String& str) {
  String encoded = "";
  char c;
  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else {
      encoded += '%';
      if (c < 16) encoded += '0';
      encoded += String(c, HEX);
    }
  }
  return encoded;
}
