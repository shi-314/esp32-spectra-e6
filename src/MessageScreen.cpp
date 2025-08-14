#include "MessageScreen.h"

#include "battery.h"

MessageScreen::MessageScreen(DisplayType& display)
    : display(display), primaryFont(u8g2_font_helvB12_tf), smallFont(u8g2_font_micro_tr), messageText("") {
  gfx.begin(display);
}

void MessageScreen::setMessageText(const String& text) { messageText = text; }

void MessageScreen::render() {
  Serial.println("Displaying AI message screen");

  display.init(115200);
  display.setRotation(1);
  display.fillScreen(GxEPD_WHITE);

  gfx.setFontMode(1);
  gfx.setFontDirection(0);
  gfx.setForegroundColor(GxEPD_BLACK);
  gfx.setBackgroundColor(GxEPD_WHITE);
  gfx.setFont(primaryFont);

  // Get display dimensions
  int displayWidth = display.width();
  int displayHeight = display.height();

  // Set margins
  int margin = 10;
  int textAreaWidth = displayWidth - (2 * margin);
  int textAreaHeight = displayHeight - (2 * margin);

  // Calculate line height
  int lineHeight = gfx.getFontAscent() - gfx.getFontDescent() + 2;

  // Word wrapping logic
  String words[100];  // Max 100 words
  int wordCount = 0;

  // Split text into words
  String tempText = messageText;
  tempText.trim();

  while (tempText.length() > 0 && wordCount < 100) {
    int spaceIndex = tempText.indexOf(' ');
    if (spaceIndex == -1) {
      words[wordCount++] = tempText;
      break;
    } else {
      words[wordCount++] = tempText.substring(0, spaceIndex);
      tempText = tempText.substring(spaceIndex + 1);
      tempText.trim();
    }
  }

  // Build lines with word wrapping
  String lines[20];  // Max 20 lines
  int lineCount = 0;
  String currentLine = "";

  for (int i = 0; i < wordCount && lineCount < 20; i++) {
    String testLine = currentLine;
    if (testLine.length() > 0) {
      testLine += " ";
    }
    testLine += words[i];

    int testWidth = gfx.getUTF8Width(testLine.c_str());

    if (testWidth <= textAreaWidth) {
      currentLine = testLine;
    } else {
      if (currentLine.length() > 0) {
        lines[lineCount++] = currentLine;
        currentLine = words[i];
      } else {
        // Single word is too long, just add it anyway
        lines[lineCount++] = words[i];
        currentLine = "";
      }
    }
  }

  // Add remaining text as last line
  if (currentLine.length() > 0 && lineCount < 20) {
    lines[lineCount++] = currentLine;
  }

  // Calculate total text height
  int totalTextHeight = lineCount * lineHeight;

  // Center the text vertically
  int startY = margin + (textAreaHeight - totalTextHeight) / 2;
  if (startY < margin) startY = margin;

  // Draw each line centered horizontally
  for (int i = 0; i < lineCount; i++) {
    int lineWidth = gfx.getUTF8Width(lines[i].c_str());
    int centerX = margin + (textAreaWidth - lineWidth) / 2;
    if (centerX < margin) centerX = margin;

    int y = startY + (i * lineHeight) + gfx.getFontAscent();

    gfx.setCursor(centerX, y);
    gfx.print(lines[i]);
  }

  // Display battery status in corner
  String batteryStatus = getBatteryStatus();
  gfx.setFont(smallFont);
  gfx.setForegroundColor(GxEPD_BLACK);

  int batteryWidth = gfx.getUTF8Width(batteryStatus.c_str());
  gfx.setCursor(displayWidth - batteryWidth - 2, displayHeight - 1);
  gfx.print(batteryStatus);

  display.setFullWindow();
  display.firstPage();
  do {
    // Content is already drawn above
  } while (display.nextPage());
  display.hibernate();
  Serial.println("Message displayed");
}

int MessageScreen::nextRefreshInSeconds() { return 3600; }
