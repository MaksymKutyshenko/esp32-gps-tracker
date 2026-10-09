# GPS Tracker

> ESP32-S3 GPS tracker on ESP-IDF and FreeRTOS: reads NMEA from a GPS
> receiver, shows the fix on a display and logs the track to storage.

Final project for the Beetroot Academy "Embedded Development" course.
Requirements and the grading checklist are in [REQUIREMENTS.md](REQUIREMENTS.md).

## About

_To be written once the component list is final._

## Quick start

```bash
pio run -t upload -t monitor   # flash and watch the logs (115200 baud)
pio test -e native             # host-side unit tests for the pure logic
```

## Wiring / schematic

_Pending component list._

## Architecture

Layers: Periph / Logic / Transport-Output / Reliability.
Planned tasks: `gps_rx`, `display`, `logger`, `ui` (see `include/App.hpp`).

## Interfaces

_UART (NMEA), I2C, SPI, ADC — to be documented._

## Results

## Known issues and limitations

## What I would improve next

## Documentation

- [REQUIREMENTS.md](REQUIREMENTS.md) — course requirements and checklist mapping
- `docs/` — schematic, diagrams, datasheets

## Author

Maksym Kutyshenko
