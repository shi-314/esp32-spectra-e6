#ifndef MESSAGE_SCREEN_H
#define MESSAGE_SCREEN_H

#include <U8g2_for_Adafruit_GFX.h>

#include "DisplayType.h"
#include "Screen.h"

class MessageScreen : public Screen {
 private:
  DisplayType& display;
  U8G2_FOR_ADAFRUIT_GFX gfx;
  String messageText;

  const uint8_t* primaryFont;
  const uint8_t* smallFont;

 public:
  MessageScreen(DisplayType& display);

  void setMessageText(const String& text);
  void render() override;
  int nextRefreshInSeconds() override;
};

#endif
