#include "ConfigurationScreen.h"

ConfigurationScreen::ConfigurationScreen(DisplayType& display)
    : display(display), accessPointName("WeatherStation-Config"), accessPointPassword("configure123") {
  gfx.begin(display);
}

String ConfigurationScreen::generateWiFiQRString() const {
  String wifiQRCodeString = "WIFI:T:WPA2;S:" + accessPointName + ";P:" + accessPointPassword + ";H:false;;";
  return wifiQRCodeString;
}

void ConfigurationScreen::drawQRCode(const String& wifiString, int x, int y, int scale) {
  const uint8_t qrCodeVersion4 = 4;
  uint8_t qrCodeDataBuffer[qrcode_getBufferSize(qrCodeVersion4)];
  QRCode qrCodeInstance;

  int qrGenerationResult =
      qrcode_initText(&qrCodeInstance, qrCodeDataBuffer, qrCodeVersion4, ECC_MEDIUM, wifiString.c_str());

  if (qrGenerationResult != 0) {
    Serial.print("Failed to generate QR code, error: ");
    Serial.println(qrGenerationResult);
    return;
  }

  for (uint8_t qrModuleY = 0; qrModuleY < qrCodeInstance.size; qrModuleY++) {
    for (uint8_t qrModuleX = 0; qrModuleX < qrCodeInstance.size; qrModuleX++) {
      bool moduleIsBlack = qrcode_getModule(&qrCodeInstance, qrModuleX, qrModuleY);
      if (moduleIsBlack) {
        for (int scaledPixelY = 0; scaledPixelY < scale; scaledPixelY++) {
          for (int scaledPixelX = 0; scaledPixelX < scale; scaledPixelX++) {
            int displayPixelX = x + (qrModuleX * scale) + scaledPixelX;
            int displayPixelY = y + (qrModuleY * scale) + scaledPixelY;

            bool pixelIsWithinDisplayBounds = displayPixelX < display.width() && displayPixelY < display.height();
            if (pixelIsWithinDisplayBounds) {
              display.drawPixel(displayPixelX, displayPixelY, GxEPD_BLACK);
            }
          }
        }
      }
    }
  }

  Serial.println("QR Code drawn to display");
}

void ConfigurationScreen::render() {
  Serial.println("Displaying configuration screen with QR code");

  display.init(115200);
  display.setRotation(1);
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);
  display.setFont();

  const int textLeftMargin = 8;
  const int textLineSpacing = 14;
  const int qrCodePixelScale = 3;
  const int qrCodeModuleCount = 33;
  const int qrCodePixelSize = qrCodeModuleCount * qrCodePixelScale;
  const int qrCodeQuietZonePixels = 4;

  int qrCodePositionX = display.width() - qrCodePixelSize - qrCodeQuietZonePixels;
  int qrCodePositionY = (display.height() - qrCodePixelSize) / 2;

  int currentTextLineY = 18;
  int availableTextWidth = qrCodePositionX - textLeftMargin - 8;

  gfx.setFont(u8g2_font_open_iconic_embedded_2x_t);
  gfx.setFontMode(1);
  gfx.setForegroundColor(GxEPD_BLACK);
  gfx.setBackgroundColor(GxEPD_WHITE);
  gfx.setCursor(textLeftMargin, currentTextLineY + 12);
  gfx.print((char)66);  // gear icon

  gfx.setFont(u8g2_font_helvB08_tr);
  gfx.setCursor(textLeftMargin + 20, currentTextLineY + 8);
  gfx.print("Config Mode");
  currentTextLineY += textLineSpacing * 2;

  gfx.setFont(u8g2_font_helvR08_tr);
  gfx.setCursor(textLeftMargin, currentTextLineY);
  gfx.println("1. Scan QR code");
  currentTextLineY += textLineSpacing;

  gfx.setCursor(textLeftMargin, currentTextLineY);
  gfx.println("2. Connect to WiFi");
  currentTextLineY += textLineSpacing;

  gfx.setCursor(textLeftMargin, currentTextLineY);
  gfx.println("3. Configure");
  currentTextLineY += textLineSpacing;

  gfx.setCursor(textLeftMargin, currentTextLineY);
  gfx.println("4. Save & Exit");

  String wifiQRCodeString = generateWiFiQRString();

  int qrCodeBackgroundX = qrCodePositionX - qrCodeQuietZonePixels;
  int qrCodeBackgroundY = qrCodePositionY - qrCodeQuietZonePixels;
  int qrCodeBackgroundWidth = qrCodePixelSize + (2 * qrCodeQuietZonePixels);
  int qrCodeBackgroundHeight = qrCodePixelSize + (2 * qrCodeQuietZonePixels);

  display.fillRect(qrCodeBackgroundX, qrCodeBackgroundY, qrCodeBackgroundWidth, qrCodeBackgroundHeight, GxEPD_WHITE);

  drawQRCode(wifiQRCodeString, qrCodePositionX, qrCodePositionY, qrCodePixelScale);

  display.setFullWindow();
  display.firstPage();
  do {
    // Content is already drawn above
  } while (display.nextPage());
  display.hibernate();

  Serial.println("Configuration screen with enhanced QR code displayed");
}

int ConfigurationScreen::nextRefreshInSeconds() { return 600; }
