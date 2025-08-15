#include "ConfigurationScreen.h"

#include <memory>

#include "HardwareSerial.h"

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

  int blackModules = 0;

  for (uint8_t qrModuleY = 0; qrModuleY < qrCodeInstance.size; qrModuleY++) {
    for (uint8_t qrModuleX = 0; qrModuleX < qrCodeInstance.size; qrModuleX++) {
      bool moduleIsBlack = qrcode_getModule(&qrCodeInstance, qrModuleX, qrModuleY);
      if (moduleIsBlack) {
        blackModules++;
        int rectX = x + (qrModuleX * scale);
        int rectY = y + (qrModuleY * scale);
        display.fillRect(rectX, rectY, scale, scale, GxEPD_BLACK);
      }
    }
  }
}

void ConfigurationScreen::render() {
  Serial.println("Displaying configuration screen with QR code");

  display.init(115200);

  const int textLeftMargin = 8;
  const int textLineSpacing = 14;
  const int qrCodePixelScale = 3;
  const int qrCodeModuleCount = 33;
  const int qrCodePixelSize = qrCodeModuleCount * qrCodePixelScale;
  const int qrCodeQuietZonePixels = 4;

  String wifiQRCodeString = generateWiFiQRString();

  int qrCodePositionX = display.width() - qrCodePixelSize - qrCodeQuietZonePixels;
  int qrCodePositionY = (display.height() - qrCodePixelSize) / 2;

  gfx.setFontMode(1);
  gfx.setForegroundColor(GxEPD_BLACK);
  gfx.setBackgroundColor(GxEPD_WHITE);

  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);

  drawQRCode(wifiQRCodeString, qrCodePositionX, qrCodePositionY, qrCodePixelScale);
  int currentTextLineY = 18;

  // Draw gear icon and title
  gfx.setFont(u8g2_font_open_iconic_embedded_2x_t);
  gfx.setCursor(textLeftMargin, currentTextLineY + 12);
  gfx.print((char)66);  // gear icon

  gfx.setFont(u8g2_font_helvB08_tr);
  gfx.setCursor(textLeftMargin + 20, currentTextLineY + 8);
  gfx.print("Config Mode");
  currentTextLineY += textLineSpacing * 2;

  // Draw instructions
  gfx.setFont(u8g2_font_helvR08_tr);
  gfx.setCursor(textLeftMargin, currentTextLineY);
  gfx.print("1. Scan QR code");
  currentTextLineY += textLineSpacing;

  gfx.setCursor(textLeftMargin, currentTextLineY);
  gfx.print("2. Connect to WiFi");
  currentTextLineY += textLineSpacing;

  gfx.setCursor(textLeftMargin, currentTextLineY);
  gfx.print("3. Configure");
  currentTextLineY += textLineSpacing;

  gfx.setCursor(textLeftMargin, currentTextLineY);
  gfx.print("4. Save & Exit");

  display.display();
  display.hibernate();

  Serial.println("Configuration screen rendered successfully");
}

int ConfigurationScreen::nextRefreshInSeconds() { return 600; }
