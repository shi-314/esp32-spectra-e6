// Renders the meteogram screen on a desktop from a saved Open-Meteo response, for design iteration
#include <fstream>
#include <sstream>

#include "IndoorSensor.h"
#include "MeteogramScreen.h"
#include "battery.h"

SerialShim Serial;
size_t Print::printf(const char*, ...) { return 0; }

IndoorReading readIndoorSensor() {
  IndoorReading r;
  r.valid = true;
  r.temperature = 23.4f;
  r.humidity = 46.0f;
  return r;
}
int getBatteryPercentage() { return 87; }
String getBatteryStatus() { return "87%"; }

// Approximate printed Spectra 6 inks, so the preview shows the muted colors of the real panel
static void ink(uint16_t c, bool panel, uint8_t* rgb) {
  struct { uint16_t c; uint8_t ideal[3], panel[3]; } inks[] = {
      {GxEPD_BLACK, {0, 0, 0}, {35, 35, 40}},       {GxEPD_WHITE, {255, 255, 255}, {225, 225, 218}},
      {GxEPD_RED, {255, 0, 0}, {170, 30, 30}},      {GxEPD_YELLOW, {255, 230, 0}, {225, 205, 50}},
      {GxEPD_BLUE, {0, 0, 255}, {40, 80, 170}},     {GxEPD_GREEN, {0, 160, 0}, {120, 150, 110}},
  };
  for (auto& i : inks)
    if (i.c == c) { memcpy(rgb, panel ? i.panel : i.ideal, 3); return; }
  rgb[0] = 255; rgb[1] = 0; rgb[2] = 255;  // Any other color is not an ink: flag it in magenta
}

bool DisplayType::writePPM(const char* path, bool panel) const {
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  fprintf(f, "P6 %d %d 255\n", W, H);
  for (int i = 0; i < W * H; i++) {
    uint8_t rgb[3];
    ink(pixels[i], panel, rgb);
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
  return true;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s forecast.json out-prefix [location]\n", argv[0]);
    return 1;
  }
  std::ifstream in(argv[1]);
  std::stringstream buffer;
  buffer << in.rdbuf();

  OpenMeteoAPI api;
  WeatherForecast forecast = api.parseForecast(String(buffer.str()));
  static DisplayType display;
  MeteogramScreen screen(display, forecast, argc > 3 ? argv[3] : "Glasgow");
  screen.render();

  display.writePPM((std::string(argv[2]) + "-ideal.ppm").c_str(), false);
  display.writePPM((std::string(argv[2]) + "-panel.ppm").c_str(), true);
  return 0;
}
