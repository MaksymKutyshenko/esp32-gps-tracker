# Changelog

Firmware and web app are versioned together; the firmware reports its
version in every health report (`fw=`).

## Unreleased

### Firmware
- SuperMini is the build board and the default environment; its image
  declares the module's 4 MB of flash.
- Modem PWRKEY through an RC filter (1 kΩ, 100 nF); `gsmPowerKeyWired`
  selects the alternative of PWRKEY tied to GND, with an AT reset instead
  of the key.
- Button wiring is a setting (`buttonPressedHigh`) for a 3-pin module
  with its own pull-down; the wake lines are checked before each sleep,
  and a button line that reads pressed at rest is not armed.
- Sleep entry turns the screen off first and waits 10 s, not 60 s, for a
  modem that never attached; a parked tracker sleeps at most 3 min after
  its parked time even with points still queued.
- A fix dated outside 2025-2045 is treated as no fix yet.
- Status LED on GPIO48 for the DevKitC.
- 85 host-side unit tests.

### Hardware
- Wiring diagram, KiCad schematic and assembly sheet redrawn for the
  SuperMini, with R5/C2 on PWRKEY and the 3-pin button module.

## 1.0.0 — 2026-10-13

First release.

### Firmware
- GNSS over UART (u-blox M10 profile with UBX setup, BE-220 profile), NMEA
  validation, quality gate on satellites and HDOP, static hold and
  accuracy mask against drift while parked.
- Adaptive track log on MicroSD: one record a second while moving, one a
  minute while parked, one CSV per UTC day; the console log mirrored to
  `EVENTS.LOG` with date and time on every line.
- GPRS uplink over a SIM800C: OsmAnd position reports and a health report
  with every diagnostics counter once a minute; back-off, bearer re-open
  and modem power cycle on failure.
- Motion from a BMA400 activity interrupt, GNSS speed as fallback; deep
  sleep after a parked period with wake on motion, the button or an
  hourly timer.
- SSD1306 status display with local time, WS2812 state LED, battery
  voltage on the ADC, settings in NVS over a serial console.
- Task watchdog on every task that records the task it caught; SD
  commands capped at 1 s; `BusLock` with timeouts on every shared bus.
- 83 host-side unit tests for the pure logic.

### Web app
- Live map with track history, module health per report, uptime and reset
  statistics, server-sent events, shared access key sign-in, device
  simulator, Fly.io deployment.

### Hardware
- KiCad schematic (ERC clean), wiring diagram and assembly sheet generated
  from the pin map; 1000 µF at the modem supply; BMA400 CS tied high.
