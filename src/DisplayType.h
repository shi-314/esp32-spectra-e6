#ifndef DISPLAY_TYPE_H
#define DISPLAY_TYPE_H

#include <GxEPD2_7C.h>

#include "boards.h"

// Display driver is selected by the board configuration (see include/boards.h)
#ifdef EPD_BUSY_TIMEOUT_US
// GxEPD2 hardcodes the busy timeout per driver; boards with slower refreshes override it here
class Epd2Type : public EPD_DRIVER {
 public:
  Epd2Type(int16_t cs, int16_t dc, int16_t rst, int16_t busy) : EPD_DRIVER(cs, dc, rst, busy) {
    _busy_timeout = EPD_BUSY_TIMEOUT_US;
  }
};
#else
using Epd2Type = EPD_DRIVER;
#endif

using DisplayType = GxEPD2_7C<Epd2Type, Epd2Type::HEIGHT>;

#endif
