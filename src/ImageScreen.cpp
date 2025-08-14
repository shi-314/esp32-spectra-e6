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

int ImageScreen::downloadAndDisplayImage() {
  String requestUrl = ditheringServiceUrl + "/process?url=" + downloader.urlEncode(String(config.imageUrl)) +
                      "&width=" + String(display.width()) + "&height=" + String(display.height()) +
                      "&dither=true&normalize=false&colors=000000,ffffff,e6e600,cc0000,0033cc,00cc00";

  String storedETag = getStoredImageETag();
  DownloadResult result = downloader.download(requestUrl, storedETag);

  if (result.httpCode == HTTP_CODE_NOT_MODIFIED) {
    Serial.println("Image not modified (304), using cached version");
    return result.httpCode;
  }

  if (result.httpCode != HTTP_CODE_OK) {
    downloader.cleanup(result);
    return result.httpCode;
  }

  if (result.etag.length() > 0) {
    storeImageETag(result.etag);
  }

  int processResult = processImageData(result.data, result.size);
  downloader.cleanup(result);
  return processResult;
}

int ImageScreen::processImageData(uint8_t* data, size_t dataSize) {
  size_t dataIndex = 0;

  if (dataSize < 54) {
    Serial.printf("Payload too small for BMP header: got %d bytes, expected at least 54\n", dataSize);
    free(data);
    return -1;
  }

  uint8_t bmpHeader[54];
  memcpy(bmpHeader, data + dataIndex, 54);
  dataIndex += 54;

  if (bmpHeader[0] != 'B' || bmpHeader[1] != 'M') {
    Serial.printf("Invalid BMP signature: got 0x%02X 0x%02X, expected 0x42 0x4D ('BM')\n", bmpHeader[0], bmpHeader[1]);
    free(data);
    return -1;
  }

  uint32_t dataOffset = bmpHeader[10] | (bmpHeader[11] << 8) | (bmpHeader[12] << 16) | (bmpHeader[13] << 24);
  uint32_t imageWidth = bmpHeader[18] | (bmpHeader[19] << 8) | (bmpHeader[20] << 16) | (bmpHeader[21] << 24);
  uint32_t imageHeight = bmpHeader[22] | (bmpHeader[23] << 8) | (bmpHeader[24] << 16) | (bmpHeader[25] << 24);
  uint16_t bitsPerPixel = bmpHeader[28] | (bmpHeader[29] << 8);
  uint32_t compression = bmpHeader[30] | (bmpHeader[31] << 8) | (bmpHeader[32] << 16) | (bmpHeader[33] << 24);

  Serial.printf("BMP Header Debug:\n");
  Serial.printf("  Data offset: %d\n", dataOffset);
  Serial.printf("  Image size: %dx%d\n", imageWidth, imageHeight);
  Serial.printf("  Bits per pixel: %d\n", bitsPerPixel);
  Serial.printf("  Compression: %d\n", compression);
  Serial.printf("  Payload size: %d\n", dataSize);

  if (bitsPerPixel != 8) {
    Serial.printf("Unsupported bits per pixel: %d (expected 8 for indexed color)\n", bitsPerPixel);
    free(data);
    return -1;
  }

  if (compression != 0) {
    Serial.printf("Unsupported compression: %d (expected 0 for uncompressed)\n", compression);
    free(data);
    return -1;
  }

  // 8-bit BMP has 256 color palette entries, each 4 bytes (BGRA)
  uint32_t paletteSize = 256 * 4;
  if (dataIndex + paletteSize > dataSize) {
    Serial.printf("Not enough data for color palette: need %d bytes, have %d\n", dataIndex + paletteSize, dataSize);
    free(data);
    return -1;
  }

  // Skip the full palette since we'll map pixel indices directly to grayscale
  dataIndex += paletteSize;

  Serial.printf("Skipped %d byte color palette\n", paletteSize);

  if (dataOffset > dataIndex) {
    uint32_t skipBytes = dataOffset - dataIndex;
    if (dataIndex + skipBytes > dataSize) {
      Serial.printf("Data offset beyond payload size: offset %d, payload size %d\n", dataOffset, dataSize);
      free(data);
      return -1;
    }
    Serial.printf("Skipping %d bytes to reach data offset %d\n", skipBytes, dataOffset);
    dataIndex += skipBytes;
  }

  Serial.printf("Starting pixel data read at index %d\n", dataIndex);

  display.init(115200);
  // Display was already correctly oriented, no rotation needed

  int offsetX = 0;
  int offsetY = 0;

  // Row size padded to 4-byte boundary
  uint32_t rowSize = ((imageWidth * bitsPerPixel + 31) / 32) * 4;
  uint8_t* rowBuffer = new uint8_t[rowSize];

  Serial.printf("BMP processing: width=%d, height=%d, bitsPerPixel=%d, rowSize=%d\n", imageWidth, imageHeight,
                bitsPerPixel, rowSize);

  // Create buffer for color pixel data (1 byte per pixel) using PSRAM
  size_t pixelBufferSize = imageWidth * imageHeight;
  uint8_t* pixelBuffer = (uint8_t*)ps_malloc(pixelBufferSize);
  if (!pixelBuffer) {
    Serial.println("Failed to allocate PSRAM for pixel buffer");
    delete[] rowBuffer;
    free(data);
    return -1;
  }
  Serial.printf("Allocated %d bytes in PSRAM for pixel buffer\n", pixelBufferSize);

  int pixelCount[4] = {0};  // Count pixels for each color level
  int totalPixelCount = 0;

  for (int y = imageHeight - 1; y >= 0; y--) {
    if (dataIndex + rowSize > dataSize) {
      Serial.printf("Not enough data for row %d: need %d bytes, have %d remaining (dataIndex=%d)\n", y, rowSize,
                    dataSize - dataIndex, dataIndex);
      delete[] rowBuffer;
      free(pixelBuffer);
      free(data);
      return -1;
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

    // Debug: Print first few rows and check for unexpected data
    if (y >= imageHeight - 3 || y <= 2) {
      Serial.printf("Row %d (BMP line %d): first 10 pixels: ", y, (imageHeight - 1) - y);
      for (int i = 0; i < 10 && i < imageWidth; i++) {
        Serial.printf("%d ", rowBuffer[i]);
      }
      Serial.println();
    }

    // Check if we're getting unexpected zeros
    int zeroCount = 0;
    for (int i = 0; i < imageWidth; i++) {
      if (rowBuffer[i] == 0) zeroCount++;
    }
    if (zeroCount > imageWidth * 0.9) {  // More than 90% zeros
      Serial.printf("WARNING: Row %d has %d zeros out of %d pixels (%.1f%%)\n", y, zeroCount, imageWidth,
                    (float)zeroCount / imageWidth * 100);
    }

    for (int x = 0; x < imageWidth; x++) {
      uint8_t pixelIndex = rowBuffer[x];
      totalPixelCount++;

      // Map 4-color palette indices directly to display colors
      // Test: Let's see if the mapping is correct by checking actual distribution
      uint8_t colorIndex = pixelIndex;  // Direct mapping first

      // We're reading BMP rows from bottom (y=imageHeight-1) to top (y=0)
      // Convert to display coordinates: display_y = (imageHeight-1) - y
      int displayY = (imageHeight - 1) - y;
      int bufferIndex = displayY * imageWidth + x;
      pixelBuffer[bufferIndex] = colorIndex;

      // Count pixels for debugging
      if (colorIndex < 4) {
        pixelCount[colorIndex]++;
      }
    }
  }

  delete[] rowBuffer;

  // Debug: Print pixel statistics for 4 colors
  Serial.printf("Processed %d pixels:\n", totalPixelCount);
  Serial.printf("  Index 0: %d pixels (%.1f%%)\n", pixelCount[0], (float)pixelCount[0] / totalPixelCount * 100.0f);
  Serial.printf("  Index 1: %d pixels (%.1f%%)\n", pixelCount[1], (float)pixelCount[1] / totalPixelCount * 100.0f);
  Serial.printf("  Index 2: %d pixels (%.1f%%)\n", pixelCount[2], (float)pixelCount[2] / totalPixelCount * 100.0f);
  Serial.printf("  Index 3: %d pixels (%.1f%%)\n", pixelCount[3], (float)pixelCount[3] / totalPixelCount * 100.0f);

  // Calculate position to center the image on display
  int displayWidth = display.width();
  int displayHeight = display.height();

  Serial.printf("Display dimensions: %dx%d, Image dimensions: %dx%d\n", displayWidth, displayHeight, imageWidth,
                imageHeight);

  // For same-size image, just position at origin
  int imageX = 0;
  int imageY = 0;

  // Only center if image is smaller than display
  if ((int)imageWidth < displayWidth) {
    imageX = (displayWidth - (int)imageWidth) / 2;
  }
  if ((int)imageHeight < displayHeight) {
    imageY = (displayHeight - (int)imageHeight) / 2;
  }

  // Ensure coordinates are valid
  imageX = max(0, imageX);
  imageY = max(0, imageY);

  // Calculate actual drawing dimensions - use full image size
  int drawWidth = (int)imageWidth;    // Don't crop width: 480
  int drawHeight = (int)imageHeight;  // Don't crop height: 800

  Serial.printf("Rendering image: %dx%d at position (%d,%d), draw size: %dx%d\n", imageWidth, imageHeight, imageX,
                imageY, drawWidth, drawHeight);

  // Create bitmap planes for efficient rendering
  // Calculate bitmap size in bytes (1 bit per pixel, padded to byte boundary)
  int bitmapWidthBytes = (imageWidth + 7) / 8;  // Round up to nearest byte
  size_t bitmapSize = bitmapWidthBytes * imageHeight;

  // Allocate bitmap planes for 7-color display
  // Palette: 000000,ffffff,e6e600,cc0000,0033cc,00cc00 (Black,White,Yellow,Red,Blue,Green)
  uint8_t* blackBitmap = (uint8_t*)ps_malloc(bitmapSize);   // Index 0: 000000 (Black)
  uint8_t* yellowBitmap = (uint8_t*)ps_malloc(bitmapSize);  // Index 2: e6e600 (Yellow)
  uint8_t* redBitmap = (uint8_t*)ps_malloc(bitmapSize);     // Index 3: cc0000 (Red)
  uint8_t* blueBitmap = (uint8_t*)ps_malloc(bitmapSize);    // Index 4: 0033cc (Blue)
  uint8_t* greenBitmap = (uint8_t*)ps_malloc(bitmapSize);   // Index 5: 00cc00 (Green, native support!)

  if (!blackBitmap || !yellowBitmap || !redBitmap || !blueBitmap || !greenBitmap) {
    Serial.println("Failed to allocate PSRAM for bitmap planes");
    if (blackBitmap) free(blackBitmap);
    if (yellowBitmap) free(yellowBitmap);
    if (redBitmap) free(redBitmap);
    if (blueBitmap) free(blueBitmap);
    if (greenBitmap) free(greenBitmap);
    free(pixelBuffer);
    free(data);
    return -1;
  }
  Serial.printf("Allocated 5x %d bytes in PSRAM for bitmap planes\n", bitmapSize);

  // Initialize bitmaps to 0 (all pixels off)
  memset(blackBitmap, 0, bitmapSize);
  memset(yellowBitmap, 0, bitmapSize);
  memset(redBitmap, 0, bitmapSize);
  memset(blueBitmap, 0, bitmapSize);
  memset(greenBitmap, 0, bitmapSize);

  Serial.printf("Creating bitmap planes: %dx%d, %d bytes per bitmap\n", imageWidth, imageHeight, bitmapSize);

  // Convert indexed color buffer to bitmap planes
  int bitmapPixelCounts[6] = {0, 0, 0, 0, 0, 0};  // Count pixels in each bitmap

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
          blackBitmap[byteIndex] |= bitMask;
          bitmapPixelCounts[0]++;
          break;
        case 1:  // ffffff - White (background - no bits set)
          break;
        case 2:  // e6e600 - Yellow
          yellowBitmap[byteIndex] |= bitMask;
          bitmapPixelCounts[2]++;
          break;
        case 3:  // cc0000 - Red
          redBitmap[byteIndex] |= bitMask;
          bitmapPixelCounts[3]++;
          break;
        case 4:  // 0033cc - Blue
          blueBitmap[byteIndex] |= bitMask;
          bitmapPixelCounts[4]++;
          break;
        case 5:  // 00cc00 - Green (native green support)
          greenBitmap[byteIndex] |= bitMask;
          bitmapPixelCounts[5]++;
          break;
        default:
          break;
      }
    }
  }

  Serial.printf("Bitmap plane pixel counts: Black=%d, Yellow=%d, Red=%d, Blue=%d, Orange=%d\n", bitmapPixelCounts[0],
                bitmapPixelCounts[2], bitmapPixelCounts[3], bitmapPixelCounts[4], bitmapPixelCounts[5]);

  // Display the image using chunked rendering due to buffer limitations
  // DisplayType uses HEIGHT/4 buffer, so we need to render in 4 chunks
  const int chunkHeight = imageHeight / 4;  // 480/4 = 120 pixels per chunk

  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);

  Serial.printf("Rendering in 4 chunks of %d pixels height each\n", chunkHeight);

  // Render all chunks to buffer without individual display updates
  display.firstPage();
  do {
    for (int chunk = 0; chunk < 4; chunk++) {
      int startY = chunk * chunkHeight;
      int endY = min(startY + chunkHeight, (int)imageHeight);
      int actualChunkHeight = endY - startY;

      Serial.printf("Drawing chunk %d: Y=%d to %d (height=%d)\n", chunk, startY, endY - 1, actualChunkHeight);

      // Calculate bitmap offsets for this chunk
      int bitmapChunkOffset = startY * bitmapWidthBytes;

      // Draw chunk portion of each bitmap to the buffer
      display.drawBitmap(imageX, imageY + startY, blackBitmap + bitmapChunkOffset, imageWidth, actualChunkHeight,
                         GxEPD_BLACK);
      display.drawBitmap(imageX, imageY + startY, yellowBitmap + bitmapChunkOffset, imageWidth, actualChunkHeight,
                         GxEPD_YELLOW);
      display.drawBitmap(imageX, imageY + startY, redBitmap + bitmapChunkOffset, imageWidth, actualChunkHeight,
                         GxEPD_RED);
      display.drawBitmap(imageX, imageY + startY, blueBitmap + bitmapChunkOffset, imageWidth, actualChunkHeight,
                         GxEPD_BLUE);
      display.drawBitmap(imageX, imageY + startY, greenBitmap + bitmapChunkOffset, imageWidth, actualChunkHeight,
                         GxEPD_GREEN);
    }
  } while (display.nextPage());

  // Clean up bitmap planes
  free(blackBitmap);
  free(yellowBitmap);
  free(redBitmap);
  free(blueBitmap);
  free(greenBitmap);
  display.hibernate();

  free(pixelBuffer);
  free(data);  // Clean up the data buffer

  return HTTP_CODE_OK;
}

void ImageScreen::render() {
  int statusCode = downloadAndDisplayImage();

  if (statusCode == HTTP_CODE_NOT_MODIFIED) {
    return;
  }

  if (statusCode == HTTP_CODE_OK) {
    // Image was successfully downloaded and displayed
    // The downloadAndDisplayImage() function already handled the display
    display.hibernate();
    return;
  }

  String errorMessage;
  switch (statusCode) {
    case -1:
      errorMessage = "Invalid image data";
      break;
    case HTTP_CODE_NOT_FOUND:
      errorMessage = "Image not found";
      break;
    case HTTP_CODE_UNSUPPORTED_MEDIA_TYPE:
      errorMessage = "Unsupported image format";
      break;
    case HTTP_CODE_NO_CONTENT:
      errorMessage = "Empty image response";
      break;
    case HTTP_CODE_REQUEST_TIMEOUT:
      errorMessage = "Request timeout";
      break;
    case HTTP_CODE_SERVICE_UNAVAILABLE:
      errorMessage = "Service unavailable";
      break;
    default:
      errorMessage = "Failed to load image";
      break;
  }

  displayError(errorMessage);
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
