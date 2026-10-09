# GPS Tracker

> ESP32-S3 GPS tracker on ESP-IDF and FreeRTOS: reads NMEA from a GPS
> receiver, shows the fix on a display and logs the track to storage.

Final project for the Beetroot Academy "Embedded Development" course.
Requirements and the grading checklist are in [REQUIREMENTS.md](REQUIREMENTS.md).

## About

_To be written once the component list is final._

## Quick start

```bash
pio run -e supermini -t upload -t monitor   # flash the SuperMini and watch the logs
pio run -e devkitc -t upload -t monitor     # same firmware on the DevKitC-1 fallback
pio test -e native                          # host-side unit tests for the pure logic
```

## Wiring / schematic

Component roles, power tree and the GPIO map for both boards are in
[docs/HARDWARE.md](docs/HARDWARE.md). The pin constants live in
[include/config.hpp](include/config.hpp).

| Board | Build | Console |
| --- | --- | --- |
| ESP32-S3 SuperMini (primary) | `pio run -e supermini -t upload -t monitor` | USB-C (USB Serial/JTAG) |
| ESP32-S3 DevKitC-1 (fallback) | `pio run -e devkitc -t upload -t monitor` | connector labelled "USB" |

## Architecture

Layers: Periph / Logic / Transport-Output / Reliability.
Planned tasks: `gps_rx`, `display`, `logger`, `ui` (see `include/App.hpp`).

## Interfaces

UART1 GNSS (NMEA), UART2 SIM800C (AT commands), I2C (SSD1306, BMA400), SPI (MicroSD), ADC (battery). Details in `docs/HARDWARE.md`.

## Results

## Known issues and limitations

## What I would improve next

## Documentation

- [REQUIREMENTS.md](REQUIREMENTS.md) — course requirements and checklist mapping
- `docs/` — schematic, diagrams, datasheets

## Author

Maksym Kutyshenko
