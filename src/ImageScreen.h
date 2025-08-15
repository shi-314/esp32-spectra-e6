#ifndef IMAGE_SCREEN_H
#define IMAGE_SCREEN_H

#include <U8g2_for_Adafruit_GFX.h>

#include "ApplicationConfig.h"
#include "DisplayType.h"
#include "HttpDownloader.h"
#include "Screen.h"

RTC_DATA_ATTR static char storedImageETag[128] = "";

struct ColorImageBitmaps {
  uint8_t* blackBitmap;
  uint8_t* yellowBitmap;
  uint8_t* redBitmap;
  uint8_t* blueBitmap;
  uint8_t* greenBitmap;
  uint32_t width;
  uint32_t height;
  size_t bitmapSize;

  ColorImageBitmaps()
      : blackBitmap(nullptr),
        yellowBitmap(nullptr),
        redBitmap(nullptr),
        blueBitmap(nullptr),
        greenBitmap(nullptr),
        width(0),
        height(0),
        bitmapSize(0) {}

  ~ColorImageBitmaps() {
    if (blackBitmap) free(blackBitmap);
    if (yellowBitmap) free(yellowBitmap);
    if (redBitmap) free(redBitmap);
    if (blueBitmap) free(blueBitmap);
    if (greenBitmap) free(greenBitmap);
  }
};

class ImageScreen : public Screen {
 private:
  DisplayType& display;
  U8G2_FOR_ADAFRUIT_GFX gfx;
  ApplicationConfig& config;
  HttpDownloader downloader;

  const uint8_t* smallFont;
  String ditheringServiceUrl;

  std::unique_ptr<DownloadResult> download();
  std::unique_ptr<ColorImageBitmaps> processImageData(uint8_t* data, size_t dataSize);
  void renderBitmaps(const ColorImageBitmaps& bitmaps);
  void displayError(const String& errorMessage);
  void displayBatteryStatus();
  void storeImageETag(const String& etag);
  String getStoredImageETag();

 public:
  ImageScreen(DisplayType& display, ApplicationConfig& config);
  void render() override;
  int nextRefreshInSeconds() override;
};

#endif
