#ifndef DISPLAY_TYPE_H
#define DISPLAY_TYPE_H

#include <GxEPD2_7C.h>

// Define here which display type to use - using 7.3" color e-paper

// Display instance for 7.3" color e-paper - using reduced buffer
using DisplayType = GxEPD2_7C<GxEPD2_730c_GDEP073E01, GxEPD2_730c_GDEP073E01::HEIGHT>;
using Epd2Type = GxEPD2_730c_GDEP073E01;

#endif
