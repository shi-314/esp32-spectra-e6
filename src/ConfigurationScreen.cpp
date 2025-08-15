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
  Serial.printf("Display dimensions: %d x %d\n", display.width(), display.height());

  const int textLeftMargin = 40;
  const int titleFontSize = 24;
  const int instructionFontSize = 18;
  const int lineSpacing = 45;
  const int qrCodeScale = 6;
  const int qrCodeModuleCount = 33;
  const int qrCodePixelSize = qrCodeModuleCount * qrCodeScale;
  const int qrCodeQuietZone = 20;

  String wifiQRCodeString = generateWiFiQRString();

  int qrCodeX = display.width() - qrCodePixelSize - qrCodeQuietZone - 40;
  int qrCodeY = (display.height() - qrCodePixelSize) / 2;

  gfx.setFontMode(1);
  gfx.setForegroundColor(GxEPD_BLUE);
  gfx.setForegroundColor(GxEPD_BLACK);

  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);

  display.fillRect(0, 0, display.width(), 80, GxEPD_BLUE);

  gfx.setFont(u8g2_font_open_iconic_embedded_4x_t);
  gfx.setForegroundColor(GxEPD_WHITE);
  gfx.setCursor(textLeftMargin, 55);
  gfx.print((char)66);

  gfx.setFont(u8g2_font_fur17_tr);
  gfx.setCursor(textLeftMargin + 40, 50);
  gfx.print("Configuration Mode");

  gfx.setBackgroundColor(GxEPD_WHITE);
  gfx.setForegroundColor(GxEPD_BLACK);

  int currentY = 140;

  gfx.setFont(u8g2_font_fur17_tr);

  gfx.setCursor(textLeftMargin, currentY);
  gfx.print("1. Scan QR code with your phone");
  currentY += lineSpacing;

  gfx.setCursor(textLeftMargin, currentY);
  gfx.print("2. Connect to WiFi network:");
  currentY += 25;

  gfx.setFont(u8g2_font_courB14_tr);
  gfx.setCursor(textLeftMargin + 30, currentY);
  gfx.print("WeatherStation-Config");
  currentY += lineSpacing;

  gfx.setFont(u8g2_font_fur17_tr);
  gfx.setCursor(textLeftMargin, currentY);
  gfx.print("3. Open web browser and configure");
  currentY += lineSpacing;

  gfx.setCursor(textLeftMargin, currentY);
  gfx.print("4. Save settings and exit");

  int qrBgX = qrCodeX - qrCodeQuietZone;
  int qrBgY = qrCodeY - qrCodeQuietZone;
  int qrBgSize = qrCodePixelSize + (2 * qrCodeQuietZone);

  display.fillRect(qrBgX - 5, qrBgY - 5, qrBgSize + 10, qrBgSize + 10, GxEPD_RED);
  display.fillRect(qrBgX, qrBgY, qrBgSize, qrBgSize, GxEPD_WHITE);

  drawQRCode(wifiQRCodeString, qrCodeX, qrCodeY, qrCodeScale);

  display.display();
  display.hibernate();

  Serial.println("Configuration screen rendered successfully");
}

int ConfigurationScreen::nextRefreshInSeconds() { return 600; }
