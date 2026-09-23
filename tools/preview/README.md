# Meteogram preview

Renders the meteogram screen on your computer instead of the e-paper panel. A full panel refresh takes
about 30 seconds, and a flash takes longer, so this is the quick way to iterate on layout, colors and
dithering.

The build compiles the real `src/MeteogramScreen.cpp` and `src/OpenMeteoAPI.cpp` against small shims
for Arduino (`shim/Arduino.h`) and the display (`shim/DisplayType.h`, an 800×480 framebuffer). The
fonts and graphics primitives come from the same Adafruit GFX and U8g2 libraries the firmware uses.
Only the forecast JSON is parsed on the desktop. Battery and indoor sensor readings are fixed stand-in
values set in `preview.cpp`.

## Requirements

- A C/C++ compiler (`c++`, `cc`) and `ffmpeg` on your `PATH`
- The firmware's libraries, downloaded by building once:
  `pio run -e reterminal-e1002`

## Usage

```bash
tools/preview/build.sh <forecast.json> <out-prefix> [location]
```

Examples:

```bash
tools/preview/build.sh tools/preview/fixtures/glasgow-rain.json tools/preview/out/glasgow
tools/preview/build.sh tools/preview/fixtures/winter-storm.json tools/preview/out/storm "Garmisch-Partenkirchen"
```

Each run writes two images:

- `<out-prefix>-ideal.png`: the framebuffer in pure colors. Zoom in to check individual pixels and
  dither patterns. Any color the panel cannot show appears as magenta.
- `<out-prefix>-panel.png`: the same image in approximate Spectra 6 ink colors. On the real panel
  green is very faint and white is off-white, so check contrast here.

Running the script without arguments only builds the renderer, at `tools/preview/build/preview`. Call it
directly as `build/preview <forecast.json> <out-prefix> [location]`. It writes PPM files.

## Forecast data

The renderer reads the same JSON the firmware downloads. To preview live data, fetch it with the URL
built in `OpenMeteoAPI::getForecast`, for example:

```bash
curl -s "https://api.open-meteo.com/v1/forecast?latitude=55.86&longitude=-4.25\
&hourly=temperature_2m,precipitation,wind_speed_10m,wind_gusts_10m,cloud_cover\
&current=temperature_2m,apparent_temperature,weather_code\
&daily=sunrise,sunset&forecast_days=2&past_hours=1&forecast_hours=24&wind_speed_unit=ms&timezone=auto" \
  > /tmp/forecast.json
```

The fixtures cover the cases most likely to break the layout:

- `glasgow-rain.json`: a real response with light morning rain. Its total cloud cover is derived from
  the separate cloud layers the firmware used to request.
- `winter-storm.json`: an edited response with sub-zero temperatures, heavy precipitation, strong
  gusts, and an evening start time, so the window spans midnight and two sunsets

## Troubleshooting

If linking fails with `tapi error: malformed file ... unknown architecture`, your Command Line Tools
linker is older than the default macOS SDK. Point the build at an older SDK:

```bash
SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk tools/preview/build.sh ...
```
