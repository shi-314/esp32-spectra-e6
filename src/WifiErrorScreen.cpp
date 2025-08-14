#include "WifiErrorScreen.h"

WifiErrorScreen::WifiErrorScreen(DisplayType& display) : display(display) { gfx.begin(display); }

void WifiErrorScreen::render() {
  Serial.println("Displaying WiFi error screen");

  display.init(115200);
  display.setRotation(1);
  gfx.setFontMode(1);
  gfx.setForegroundColor(GxEPD_BLACK);
  gfx.setBackgroundColor(GxEPD_WHITE);

  gfx.setFont(u8g2_font_open_iconic_www_4x_t);
  const char wifiIcon[] = "Q";
  int wifiIconWidth = gfx.getUTF8Width(wifiIcon);
  int wifiIconHeight = gfx.getFontAscent() - gfx.getFontDescent();
  int wifiIconAscent = gfx.getFontAscent();

  gfx.setFont(u8g2_font_helvR12_tr);
  String errorMessage = "WiFi Error";
  int errorMessageWidth = gfx.getUTF8Width(errorMessage.c_str());
  int errorMessageHeight = gfx.getFontAscent() - gfx.getFontDescent();
  int errorMessageAscent = gfx.getFontAscent();

  int textSpacing = 16;
  int totalGroupHeight = wifiIconHeight + textSpacing + errorMessageHeight;

  int groupCenterY = display.height() / 2;
  int groupTopY = groupCenterY - (totalGroupHeight / 2);

  int wifiIconCenterX = (display.width() / 2) - (wifiIconWidth / 2);
  int wifiIconY = groupTopY + wifiIconAscent;

  gfx.setFont(u8g2_font_open_iconic_www_4x_t);
  gfx.setCursor(wifiIconCenterX, wifiIconY);
  gfx.print(wifiIcon);

  int errorMessageX = (display.width() / 2) - (errorMessageWidth / 2);
  int errorMessageY = groupTopY + wifiIconHeight + textSpacing + errorMessageAscent;

  gfx.setFont(u8g2_font_helvR12_tr);
  gfx.setCursor(errorMessageX, errorMessageY);
  gfx.print(errorMessage);

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    gfx.setFont(u8g2_font_open_iconic_www_4x_t);
    gfx.setCursor(wifiIconCenterX, wifiIconY);
    gfx.print(wifiIcon);

    gfx.setFont(u8g2_font_helvR12_tr);
    gfx.setCursor(errorMessageX, errorMessageY);
    gfx.print(errorMessage);
  } while (display.nextPage());
}

int WifiErrorScreen::nextRefreshInSeconds() { return 600; }
