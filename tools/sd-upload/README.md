# SD card uploader

Copies files from your computer to the microSD card of the reTerminal E1002 over USB, without
removing the card. It's a separate, minimal firmware, so the main firmware carries no upload code.
Flash it, run the upload, then flash the main firmware again.

```bash
# 1. Flash the uploader
pio run -d tools/sd-upload -t upload

# 2. Copy a directory to the card (here the moon images to /moon)
~/.platformio/penv/bin/python tools/sd-upload/upload.py /dev/cu.usbserial-10 tools/moon/images /moon

# 3. Flash the main firmware again
pio run -e reterminal-e1002 -t upload
```

`upload.py` needs `pyserial`, which PlatformIO's Python already has. Replace
`/dev/cu.usbserial-10` with your device's serial port (`pio device list` shows it).

The transfer runs at 460800 baud (the board's CH340 USB bridge garbles 921600) in 4 KB chunks. The device acknowledges each chunk and reports a
CRC-32 for each file, which the script checks. Existing files with the same name are overwritten.
When it's done, the script prints how many files are in the directory and how full the card is.
