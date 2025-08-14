#pragma once

#include <Arduino.h>
#include <HTTPClient.h>

struct DownloadResult {
  uint8_t* data;
  size_t size;
  int httpCode;
  String etag;

  DownloadResult() : data(nullptr), size(0), httpCode(0), etag("") {}
};

class HttpDownloader {
 public:
  HttpDownloader();
  ~HttpDownloader();

  DownloadResult download(const String& url, const String& cachedETag = "");
  void cleanup(DownloadResult& result);
  String urlEncode(const String& str);

 private:
  DownloadResult downloadChunked(WiFiClient* stream);
  DownloadResult downloadRegular(WiFiClient* stream);
};