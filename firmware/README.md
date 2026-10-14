# Firmware

ESP-IDF 5.5 / FreeRTOS firmware for the ESP32-S3 tracker, built with
PlatformIO. The project overview, wiring, architecture, interfaces and
results are in the [repository README](../README.md); the design documents
are in [docs/](../docs/).

```bash
cd firmware
pio run -t upload -t monitor                # ESP32-S3 SuperMini (default env): build, flash, watch the log
pio run -e devkitc -t upload -t monitor     # same firmware on the DevKitC-1
pio test -e native                          # host-side unit tests for the pure logic
```

| Path | Contents |
| --- | --- |
| `include/config.hpp` | Every pin, bus speed, timeout and design value, with the reason for each |
| `include/logic/`, `test/test_native/` | Pure logic (parser, FSM, policies, calendar), unit-tested on the host |
| `include/periph/`, `src/periph/` | One class per peripheral: UART, GNSS, modem, I2C, OLED, SD card, ADC, button, LED, BMA400 |
| `include/transport/`, `src/transport/` | Track logger, uplink protocol and scheduler, status view, health report |
| `include/reliability/`, `src/reliability/` | Watchdog subscription, bus lock, NVS settings, diagnostics, event log |
| `src/App.cpp` | Composition root: owns the objects, creates the tasks and queues |
| `sdkconfig.defaults` | ESP-IDF options that differ from the defaults (task watchdog, log timestamps, run-time stats) |

Design values are changed in `config.hpp`, not at run time, so that the
behaviour of a build is visible in one file. Field settings (log intervals,
server URL, APN, device id) live in NVS and are set over the serial console
(`help`).
