#!/bin/bash
# Builds a desktop preview of the meteogram screen and renders a saved Open-Meteo response to PNGs:
# <out>-ideal.png with pure colors and <out>-panel.png with approximated Spectra 6 inks.
# Usage: tools/preview/build.sh forecast.json out-prefix [location]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
libs="$root/.pio/libdeps/reterminal-e1002"
build="$here/build"
mkdir -p "$build/src"

# Copy the screen sources next to the shims, so their quoted includes pick up the fake display
cp "$root"/src/{MeteogramScreen.cpp,MeteogramScreen.h,OpenMeteoAPI.cpp,OpenMeteoAPI.h,ApplicationConfig.h} \
  "$root"/src/{Screen.h,IndoorSensor.h,battery.h,config_default.h} "$here/shim/DisplayType.h" "$build/src/"

includes=(-I"$build/src" -I"$here/shim" -I"$root/include" -I"$libs/Adafruit GFX Library"
  -I"$libs/U8g2_for_Adafruit_GFX/src" -I"$libs/ArduinoJson/src")

[ -f "$build/u8g2_fonts.o" ] || cc -O2 -w -c "$libs/U8g2_for_Adafruit_GFX/src/u8g2_fonts.c" -o "$build/u8g2_fonts.o"
c++ -std=gnu++17 -O2 -Wall -Wno-unused-function -DARDUINO=100 -DBOARD_RETERMINAL_E1002 -DARDUINOJSON_ENABLE_PROGMEM=0 -DARDUINOJSON_ENABLE_ARDUINO_STREAM=0 "${includes[@]}" \
  "$here/preview.cpp" "$build/src/MeteogramScreen.cpp" "$build/src/OpenMeteoAPI.cpp" \
  "$libs/Adafruit GFX Library/Adafruit_GFX.cpp" "$libs/U8g2_for_Adafruit_GFX/src/U8g2_for_Adafruit_GFX.cpp" \
  "$build/u8g2_fonts.o" -o "$build/preview"

if [ $# -ge 2 ]; then
  mkdir -p "$(dirname "$2")"
  "$build/preview" "$@"
  for kind in ideal panel; do
    ffmpeg -loglevel error -y -i "$2-$kind.ppm" "$2-$kind.png" && rm "$2-$kind.ppm"
  done
fi
