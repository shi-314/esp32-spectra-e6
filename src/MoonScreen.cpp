#include "MoonScreen.h"

#include <SD.h>

#include "boards.h"

namespace {
// Frames on the card, evenly spaced over one lunation: /moon/00.bin is new moon, /moon/30.bin full
const int FRAME_COUNT = 60;
const char* FRAME_DIRECTORY = "/moon";
}  // namespace

MoonScreen::MoonScreen(DisplayType& display, SPIClass& spi, float phase) : display(display), spi(spi), phase(phase) {}

void MoonScreen::render() {
  Serial.println("Displaying moon screen");

  display.init(115200);
  display.setRotation(ApplicationConfig::DISPLAY_ROTATION);
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);

  if (isnan(phase)) {
    drawMessage("Moon phase unavailable");
  } else {
    int frame = (int)roundf(phase * FRAME_COUNT) % FRAME_COUNT;
    char path[24];
    snprintf(path, sizeof(path), "%s/%02d.bin", FRAME_DIRECTORY, frame);
    Serial.printf("Moon phase %.3f -> %s\n", phase, path);
    if (!drawMoon(path)) drawMessage("Moon images missing on SD card");
  }

  display.display();
  display.hibernate();
  Serial.println("Display updated");
}

// Reads a 1-bit moon bitmap ("MOON", width, height, then MSB-first rows) and draws its lit pixels,
// centred, into the frame buffer
bool MoonScreen::drawMoon(const String& path) {
#ifdef SD_MISO_PIN
  if (!SD.begin(SD_CS_PIN, spi, SD_SPI_FREQUENCY)) {
    Serial.println("SD card not found");
    return false;
  }

  File file = SD.open(path);
  uint8_t header[8];
  if (!file || file.read(header, sizeof(header)) != sizeof(header) || memcmp(header, "MOON", 4) != 0) {
    Serial.println("Moon image missing or invalid: " + path);
    if (file) file.close();
    SD.end();
    return false;
  }

  int width = header[4] | (header[5] << 8);
  int height = header[6] | (header[7] << 8);
  int rowBytes = (width + 7) / 8;
  int left = (display.width() - width) / 2;
  int top = (display.height() - height) / 2;

  uint8_t row[128];
  bool complete = rowBytes <= (int)sizeof(row);
  for (int y = 0; complete && y < height; y++) {
    if (file.read(row, rowBytes) != rowBytes) {
      complete = false;
      break;
    }
    for (int x = 0; x < width; x++) {
      if (row[x / 8] & (0x80 >> (x % 8))) display.drawPixel(left + x, top + y, GxEPD_WHITE);
    }
  }

  file.close();
  SD.end();
  if (!complete) Serial.println("Moon image truncated: " + path);
  return complete;
#else
  return false;
#endif
}

void MoonScreen::drawMessage(const String& message) {
  U8G2_FOR_ADAFRUIT_GFX gfx;
  gfx.begin(display);
  gfx.setFont(u8g2_font_helvB12_tf);
  gfx.setFontMode(1);
  gfx.setForegroundColor(GxEPD_WHITE);
  gfx.setCursor((display.width() - gfx.getUTF8Width(message.c_str())) / 2, display.height() / 2);
  gfx.print(message);
}

// The phase moves one frame in about 12 hours, so a few refreshes a day keep it current
int MoonScreen::nextRefreshInSeconds() { return 6 * 3600; }
