// Receives files over USB serial and writes them to the microSD card.
//
// Line-based protocol, driven by upload.py:
//   PUT <path> <size>  -> "GO", then the raw bytes in chunks of CHUNK_SIZE, each answered with "ACK",
//                         and finally "DONE <crc32 as hex>"
//   LS <dir>           -> "F <name> <size>" per file, then "END"
//   INFO               -> "INFO <card MB> <used MB>"
// Any failure is reported as "ERR <reason>".

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>

#include "boards.h"

static const unsigned long BAUD = 460800;  // The CH340 bridge garbles 921600
static const size_t CHUNK_SIZE = 4096;
static const unsigned long BYTE_TIMEOUT_MS = 5000;

SPIClass sdSpi(EPD_SPI_HOST);
static uint8_t buffer[CHUNK_SIZE];

// Standard CRC-32 (as zlib.crc32), so the host can verify each file
static uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t length) {
  crc = ~crc;
  while (length--) {
    crc ^= *data++;
    for (int bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
  }
  return ~crc;
}

static void makeParentDirectories(const String& path) {
  for (int slash = path.indexOf('/', 1); slash > 0; slash = path.indexOf('/', slash + 1)) {
    String directory = path.substring(0, slash);
    if (!SD.exists(directory)) SD.mkdir(directory);
  }
}

static void receiveFile(const String& path, size_t size) {
  makeParentDirectories(path);
  File file = SD.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("ERR cannot open " + path);
    return;
  }

  Serial.println("GO");
  uint32_t crc = 0;
  size_t received = 0;
  while (received < size) {
    size_t expected = min(CHUNK_SIZE, size - received);
    size_t filled = 0;
    unsigned long lastByte = millis();
    while (filled < expected) {
      int available = Serial.available();
      if (available > 0) {
        filled += Serial.readBytes(buffer + filled, min((size_t)available, expected - filled));
        lastByte = millis();
      } else if (millis() - lastByte > BYTE_TIMEOUT_MS) {
        file.close();
        SD.remove(path);
        Serial.println("ERR timeout");
        return;
      }
    }

    if (file.write(buffer, filled) != filled) {
      file.close();
      SD.remove(path);
      Serial.println("ERR write failed");
      return;
    }
    crc = crc32Update(crc, buffer, filled);
    received += filled;
    Serial.println("ACK");
  }

  file.close();
  Serial.printf("DONE %08x\n", crc);
}

static void listDirectory(const String& path) {
  File directory = SD.open(path);
  if (directory && directory.isDirectory()) {
    for (File entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
      Serial.printf("F %s %u\n", entry.name(), (unsigned)entry.size());
    }
  }
  Serial.println("END");
}

void setup() {
  Serial.setRxBufferSize(2 * CHUNK_SIZE);
  Serial.begin(BAUD);

  // The card's pull-ups sit on a switched rail, and the display must stay deselected on the shared bus
  pinMode(EPD_CS, OUTPUT);
  digitalWrite(EPD_CS, HIGH);
  pinMode(SD_POWER_PIN, OUTPUT);
  digitalWrite(SD_POWER_PIN, HIGH);
  delay(50);

  sdSpi.begin(EPD_SCLK, SD_MISO_PIN, EPD_MOSI, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN, sdSpi, SD_SPI_FREQUENCY)) {
    Serial.println("ERR no card");
  }
}

void loop() {
  if (!Serial.available()) return;

  String line = Serial.readStringUntil('\n');
  line.trim();

  if (line == "PING") {
    Serial.println(SD.cardType() == CARD_NONE ? "ERR no card" : "READY");
  } else if (line == "INFO") {
    Serial.printf("INFO %llu %llu\n", SD.cardSize() / (1024 * 1024), SD.usedBytes() / (1024 * 1024));
  } else if (line.startsWith("LS ")) {
    listDirectory(line.substring(3));
  } else if (line.startsWith("PUT ")) {
    int space = line.lastIndexOf(' ');
    receiveFile(line.substring(4, space), line.substring(space + 1).toInt());
  } else if (line.length() > 0) {
    Serial.println("ERR unknown command");
  }
}
