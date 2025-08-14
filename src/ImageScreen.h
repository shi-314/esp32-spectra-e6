#ifndef IMAGE_SCREEN_H
#define IMAGE_SCREEN_H

#include <U8g2_for_Adafruit_GFX.h>

#include "ApplicationConfig.h"
#include "DisplayType.h"
#include "HttpDownloader.h"
#include "Screen.h"

RTC_DATA_ATTR static char storedImageETag[128] = "";

class ImageScreen : public Screen {
 private:
  DisplayType& display;
  U8G2_FOR_ADAFRUIT_GFX gfx;
  ApplicationConfig& config;
  HttpDownloader downloader;

  const uint8_t* smallFont;
  String ditheringServiceUrl;

  int downloadAndDisplayImage();
  int processImageData(uint8_t* data, size_t dataSize);
  void displayError(const String& errorMessage);
  void storeImageETag(const String& etag);
  String getStoredImageETag();

 public:
  ImageScreen(DisplayType& display, ApplicationConfig& config);
  void render() override;
  int nextRefreshInSeconds() override;
};

#endif
