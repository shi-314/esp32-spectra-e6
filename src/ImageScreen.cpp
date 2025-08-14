#include "ImageScreen.h"

#include <SPIFFS.h>
#include <WiFi.h>

#include "battery.h"

ImageScreen::ImageScreen(DisplayType& display, ApplicationConfig& config)
    : display(display),
      config(config),
      smallFont(u8g2_font_helvR08_tr),
      ditheringServiceUrl("https://dither.lab.shvn.dev") {
  gfx.begin(display);
}

void ImageScreen::storeImageETag(const String& etag) {
  strncpy(storedImageETag, etag.c_str(), sizeof(storedImageETag) - 1);
  storedImageETag[sizeof(storedImageETag) - 1] = '\0';
  Serial.println("Stored ETag: " + etag);
}

String ImageScreen::getStoredImageETag() { return String(storedImageETag); }

std::unique_ptr<DownloadResult> ImageScreen::download() {
  String requestUrl = ditheringServiceUrl + "/process?url=" + downloader.urlEncode(String(config.imageUrl)) +
                      "&width=" + String(display.width()) + "&height=" + String(display.height()) +
                      "&dither=true&normalize=false&colors=000000,ffffff,e6e600,cc0000,0033cc,00cc00";

  String storedETag = getStoredImageETag();
  auto result = downloader.download(requestUrl, storedETag);

  if (result->etag.length() > 0) {
    storeImageETag(result->etag);
  }

  return result;
}

std::unique_ptr<ColorImageBitmaps> ImageScreen::processImageData(uint8_t* data, size_t dataSize) {
  size_t dataIndex = 0;

  if (dataSize < 54) {
    Serial.printf("Payload too small for BMP header: got %d bytes, expected at least 54\n", dataSize);
    return nullptr;
  }

  uint8_t bmpHeader[54];
  memcpy(bmpHeader, data + dataIndex, 54);
  dataIndex += 54;

  if (bmpHeader[0] != 'B' || bmpHeader[1] != 'M') {
    Serial.printf("Invalid BMP signature: got 0x%02X 0x%02X, expected 0x42 0x4D ('BM')\n", bmpHeader[0], bmpHeader[1]);
    return nullptr;
  }

  uint32_t dataOffset = bmpHeader[10] | (bmpHeader[11] << 8) | (bmpHeader[12] << 16) | (bmpHeader[13] << 24);
  uint32_t imageWidth = bmpHeader[18] | (bmpHeader[19] << 8) | (bmpHeader[20] << 16) | (bmpHeader[21] << 24);
  uint32_t imageHeight = bmpHeader[22] | (bmpHeader[23] << 8) | (bmpHeader[24] << 16) | (bmpHeader[25] << 24);
  uint16_t bitsPerPixel = bmpHeader[28] | (bmpHeader[29] << 8);
  uint32_t compression = bmpHeader[30] | (bmpHeader[31] << 8) | (bmpHeader[32] << 16) | (bmpHeader[33] << 24);

  if (bitsPerPixel != 8) {
    Serial.printf("Unsupported bits per pixel: %d (expected 8 for indexed color)\n", bitsPerPixel);
    return nullptr;
  }

  if (compression != 0) {
    Serial.printf("Unsupported compression: %d (expected 0 for uncompressed)\n", compression);
    return nullptr;
  }

  // 8-bit BMP has 256 color palette entries, each 4 bytes (BGRA)
  uint32_t paletteSize = 256 * 4;
  if (dataIndex + paletteSize > dataSize) {
    Serial.printf("Not enough data for color palette: need %d bytes, have %d\n", dataIndex + paletteSize, dataSize);
    return nullptr;
  }

  // Skip the full palette since we'll map pixel indices directly to grayscale
  dataIndex += paletteSize;

  if (dataOffset > dataIndex) {
    uint32_t skipBytes = dataOffset - dataIndex;
    if (dataIndex + skipBytes > dataSize) {
      Serial.printf("Data offset beyond payload size: offset %d, payload size %d\n", dataOffset, dataSize);
      return nullptr;
    }
    dataIndex += skipBytes;
  }

  // Row size padded to 4-byte boundary
  uint32_t rowSize = ((imageWidth * bitsPerPixel + 31) / 32) * 4;
  uint8_t* rowBuffer = new uint8_t[rowSize];

  // Create buffer for color pixel data (1 byte per pixel) using PSRAM
  size_t pixelBufferSize = imageWidth * imageHeight;
  uint8_t* pixelBuffer = (uint8_t*)ps_malloc(pixelBufferSize);
  if (!pixelBuffer) {
    Serial.println("Failed to allocate PSRAM for pixel buffer");
    delete[] rowBuffer;
    return nullptr;
  }

  for (int y = imageHeight - 1; y >= 0; y--) {
    if (dataIndex + rowSize > dataSize) {
      Serial.printf("Not enough data for row %d: need %d bytes, have %d remaining (dataIndex=%d)\n", y, rowSize,
                    dataSize - dataIndex, dataIndex);
      delete[] rowBuffer;
      free(pixelBuffer);
      return nullptr;
    }

    // Ensure we don't read beyond data bounds
    if (dataIndex + rowSize <= dataSize) {
      memcpy(rowBuffer, data + dataIndex, rowSize);
    } else {
      // Fill with zeros if we're beyond data bounds (should not happen)
      Serial.printf("WARNING: Row %d beyond data bounds, filling with zeros\n", y);
      memset(rowBuffer, 0, rowSize);
    }
    dataIndex += rowSize;

    for (int x = 0; x < imageWidth; x++) {
      uint8_t pixelIndex = rowBuffer[x];
      // Map palette indices directly to display colors
      uint8_t colorIndex = pixelIndex;  // Direct mapping first

      // We're reading BMP rows from bottom (y=imageHeight-1) to top (y=0)
      // Convert to display coordinates: display_y = (imageHeight-1) - y
      int displayY = (imageHeight - 1) - y;
      int bufferIndex = displayY * imageWidth + x;
      pixelBuffer[bufferIndex] = colorIndex;
    }
  }

  delete[] rowBuffer;

  // Create bitmap planes for color data
  // Calculate bitmap size in bytes (1 bit per pixel, padded to byte boundary)
  int bitmapWidthBytes = (imageWidth + 7) / 8;  // Round up to nearest byte
  size_t bitmapSize = bitmapWidthBytes * imageHeight;

  // Create ColorImageBitmaps struct
  auto bitmaps = std::unique_ptr<ColorImageBitmaps>(new ColorImageBitmaps());
  bitmaps->width = imageWidth;
  bitmaps->height = imageHeight;
  bitmaps->bitmapSize = bitmapSize;

  // Allocate bitmap planes for 7-color display
  // Palette: 000000,ffffff,e6e600,cc0000,0033cc,00cc00 (Black,White,Yellow,Red,Blue,Green)
  bitmaps->blackBitmap = (uint8_t*)ps_malloc(bitmapSize);   // Index 0: 000000 (Black)
  bitmaps->yellowBitmap = (uint8_t*)ps_malloc(bitmapSize);  // Index 2: e6e600 (Yellow)
  bitmaps->redBitmap = (uint8_t*)ps_malloc(bitmapSize);     // Index 3: cc0000 (Red)
  bitmaps->blueBitmap = (uint8_t*)ps_malloc(bitmapSize);    // Index 4: 0033cc (Blue)
  bitmaps->greenBitmap = (uint8_t*)ps_malloc(bitmapSize);   // Index 5: 00cc00 (Green, native support!)

  if (!bitmaps->blackBitmap || !bitmaps->yellowBitmap || !bitmaps->redBitmap || !bitmaps->blueBitmap ||
      !bitmaps->greenBitmap) {
    Serial.println("Failed to allocate PSRAM for bitmap planes");
    free(pixelBuffer);
    return nullptr;
  }

  // Initialize bitmaps to 0 (all pixels off)
  memset(bitmaps->blackBitmap, 0, bitmapSize);
  memset(bitmaps->yellowBitmap, 0, bitmapSize);
  memset(bitmaps->redBitmap, 0, bitmapSize);
  memset(bitmaps->blueBitmap, 0, bitmapSize);
  memset(bitmaps->greenBitmap, 0, bitmapSize);

  // Convert indexed color buffer to bitmap planes

  for (int y = 0; y < imageHeight; y++) {
    for (int x = 0; x < imageWidth; x++) {
      int bufferIndex = y * imageWidth + x;
      uint8_t colorIndex = pixelBuffer[bufferIndex];

      // Calculate bit position in bitmap - flip X coordinate to fix horizontal mirroring
      int flippedX = (imageWidth - 1) - x;  // Reverse X coordinate
      int byteIndex = y * bitmapWidthBytes + flippedX / 8;
      int bitIndex = 7 - (flippedX % 8);  // MSB first
      uint8_t bitMask = 1 << bitIndex;

      // Set appropriate bit in the corresponding color plane
      // Spectra 6 color mapping: 000000,ffffff,e6e600,cc0000,0033cc,00cc00
      switch (colorIndex) {
        case 0:  // 000000 - Black
          bitmaps->blackBitmap[byteIndex] |= bitMask;
          break;
        case 1:  // ffffff - White (background - no bits set)
          break;
        case 2:  // e6e600 - Yellow
          bitmaps->yellowBitmap[byteIndex] |= bitMask;
          break;
        case 3:  // cc0000 - Red
          bitmaps->redBitmap[byteIndex] |= bitMask;
          break;
        case 4:  // 0033cc - Blue
          bitmaps->blueBitmap[byteIndex] |= bitMask;
          break;
        case 5:  // 00cc00 - Green (native green support)
          bitmaps->greenBitmap[byteIndex] |= bitMask;
          break;
        default:
          break;
      }
    }
  }

  free(pixelBuffer);

  return bitmaps;
}

void ImageScreen::renderBitmaps(const ColorImageBitmaps& bitmaps) {
  display.init(115200);

  // Calculate position to center the image on display
  int displayWidth = display.width();
  int displayHeight = display.height();

  // For same-size image, just position at origin
  int imageX = 0;
  int imageY = 0;

  // Only center if image is smaller than display
  if ((int)bitmaps.width < displayWidth) {
    imageX = (displayWidth - (int)bitmaps.width) / 2;
  }
  if ((int)bitmaps.height < displayHeight) {
    imageY = (displayHeight - (int)bitmaps.height) / 2;
  }

  // Ensure coordinates are valid
  imageX = max(0, imageX);
  imageY = max(0, imageY);

  // Display the image using chunked rendering due to buffer limitations
  // DisplayType uses HEIGHT/4 buffer, so we need to render in 4 chunks
  const int chunkHeight = bitmaps.height / 4;      // 480/4 = 120 pixels per chunk
  int bitmapWidthBytes = (bitmaps.width + 7) / 8;  // Round up to nearest byte

  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);

  // Render all chunks to buffer without individual display updates
  display.firstPage();
  do {
    for (int chunk = 0; chunk < 4; chunk++) {
      int startY = chunk * chunkHeight;
      int endY = min(startY + chunkHeight, (int)bitmaps.height);
      int actualChunkHeight = endY - startY;

      // Calculate bitmap offsets for this chunk
      int bitmapChunkOffset = startY * bitmapWidthBytes;

      // Draw chunk portion of each bitmap to the buffer
      display.drawBitmap(imageX, imageY + startY, bitmaps.blackBitmap + bitmapChunkOffset, bitmaps.width,
                         actualChunkHeight, GxEPD_BLACK);
      display.drawBitmap(imageX, imageY + startY, bitmaps.yellowBitmap + bitmapChunkOffset, bitmaps.width,
                         actualChunkHeight, GxEPD_YELLOW);
      display.drawBitmap(imageX, imageY + startY, bitmaps.redBitmap + bitmapChunkOffset, bitmaps.width,
                         actualChunkHeight, GxEPD_RED);
      display.drawBitmap(imageX, imageY + startY, bitmaps.blueBitmap + bitmapChunkOffset, bitmaps.width,
                         actualChunkHeight, GxEPD_BLUE);
      display.drawBitmap(imageX, imageY + startY, bitmaps.greenBitmap + bitmapChunkOffset, bitmaps.width,
                         actualChunkHeight, GxEPD_GREEN);
    }
  } while (display.nextPage());

  display.hibernate();
}

void ImageScreen::render() {
  auto downloadResult = download();

  if (downloadResult->httpCode == HTTP_CODE_NOT_MODIFIED) {
    Serial.println("Image not modified (304), using cached version");
    return;
  }

  if (downloadResult->httpCode != HTTP_CODE_OK) {
    displayError("Failed to download image");
    return;
  }

  auto bitmaps = processImageData(downloadResult->data, downloadResult->size);
  if (!bitmaps) {
    displayError("Failed to process image data");
    return;
  }

  renderBitmaps(*bitmaps);
}

void ImageScreen::displayError(const String& errorMessage) {
  Serial.println("ImageScreen Error: " + errorMessage);

  display.init(115200);
  display.setRotation(1);

  gfx.setFontMode(1);
  gfx.setForegroundColor(GxEPD_BLACK);
  gfx.setBackgroundColor(GxEPD_WHITE);

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    gfx.setFont(smallFont);

    int textWidth = gfx.getUTF8Width(errorMessage.c_str());
    int textHeight = gfx.getFontAscent() - gfx.getFontDescent();

    int x = (display.width() - textWidth) / 2;
    int y = (display.height() + textHeight) / 2;

    gfx.setCursor(x, y);
    gfx.print(errorMessage);
  } while (display.nextPage());
}

int ImageScreen::nextRefreshInSeconds() { return 900; }
