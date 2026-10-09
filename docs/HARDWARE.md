# Hardware design

Component roles, power tree, GPIO map and the wiring risks for the GPS
tracker. Pin numbers here are the single source for `include/config.hpp`;
change them together.

Schematic: [kicad/gps_tracker.kicad_sch](kicad/gps_tracker.kicad_sch)
([PDF](kicad/gps_tracker.pdf)), generated from this pin map by
`tools/gen_schematic.py`. Breakout modules are drawn as pin headers with
their pin meaning in a note next to each; edit the script, not the file,
when a pin changes.

## 1. Components and roles

| Component | Role in the tracker | Interface | Supply |
| --- | --- | --- | --- |
| ESP32-S3 SuperMini | Main controller (4 MB flash, 2 MB PSRAM unused). No USB-UART bridge: console and flashing go through the chip's USB Serial/JTAG | — | 5 V in → onboard 3V3 LDO |
| ESP32-S3 DevKitC-1 (WROOM-1 module) | Development board and fallback: same firmware, `devkitc` is the default build environment. Its CP2102 UART console survives deep sleep and crashes, which the SuperMini's USB Serial/JTAG does not | — | USB or 5 V |
| Foxeer M10Q-250 | Primary GNSS (u-blox M10 class, GPS+GLONASS+Galileo+BDS, up to 10 Hz). Default 115200 baud, NMEA/UBX. Onboard QMC5883L (or IST8310 on V2) compass on I2C. Backup cell for warm starts | UART + I2C (compass) | 5 V in, 3.3 V logic |
| Beitian BE-220 | Alternative GNSS (u-blox M8030 class per most listings; some say M10050). Default 9600 baud, NMEA | UART | 3.0–5.5 V in, 3.3 V logic |
| BIGGA GSM (SIM800C, Ukrainian board, schematic at arduino.ua) | GPRS uplink for live position upload (2G only) | UART (AT commands) + BOOT (= PWRKEY) | 5 V on VCC → MIC29302 → 3.64 V modem rail; 2 A bursts; ~600–900 µF onboard |
| GSM SMA antenna | Antenna for the SIM800C | SMA / IPEX pigtail | — |
| BMA400 (CJMCU-400) | Motion detection: wakes the tracker from deep sleep and drives the adaptive logging rate. Not delivered yet, so the firmware treats it as optional | I2C, INT1 → GPIO | 3.3 V |
| SSD1306 0.96" OLED | Status display: fix state, satellites, speed, coordinates, battery, upload state | I2C 0x3C | 3.3 V |
| MicroSD SPI module + card | Track log (CSV/GPX) and upload backlog while offline | SPI (with DMA) | 3.3 V (or 5 V if the module has a level shifter) |
| Battery Shield V8 + 2×18650 HG2 | Power: cells in parallel (≈6000 mAh), 5 V boost output (≈2.2 A continuous, 3 A peak), 3 V/3.3 V 1 A rail, charging at 0.6–0.8 A | — | — |
| 100 µF low-ESR + 100 nF ceramic | At the BIGGA board's VCC/GND pins. The board carries 100 + 10 + 1 µF on VCC and 470/820 + 100 + 22 + 1 µF on the modem rail, so the 1000 µF is not needed (see §2) | — | — |
| 6.8 kΩ resistor | From BIGGA TX to GND: level divider for the 5 V TX line (see §5) | — | — |
| Red LED + 1 kΩ | On BIGGA NET (NETLIGHT): blink rate shows searching / registered / GPRS without any firmware | — | — |

## 2. Power tree

```
2×18650 HG2 ──► Battery Shield V8 ──► 5V rail ──┬─► ESP32-S3 SuperMini (5V pin → onboard LDO → 3V3)
   (parallel)   (boost + protection)             ├─► SIM800C board 5V in  ◄── 100 µF + 100 nF here (board has 820 µF on VBAT)
                                                 ├─► Foxeer M10Q-250 (5V in)  /  BE-220 (3.0–5.5 V)
                                                 └─► MicroSD module, only if it is the 5 V level-shifter type
                                      3V rail ───┬─► SSD1306 OLED
                                      (1 A)      ├─► BMA400
                                                 └─► MicroSD module, if it is the bare 3.3 V type
                                      GND ────────── common ground for everything, star point at the shield
```

Design notes:

- Power the 3.3 V peripherals from the **shield's 3 V rail**, not from the
  SuperMini's 3V3 pin. The SuperMini's LDO is small and SD card writes peak at
  100–200 mA. Measure the rail first: listings disagree whether it is 3.0 V
  or 3.3 V. Below 3.1 V the SD card and OLED get marginal; if it is a 3.0 V
  rail, use the ESP's 3V3 pin for the OLED and BMA400 only, and feed the SD
  module from the 5 V rail through its own regulator (level-shifter type).
- The SIM800C's transmit bursts reach 2 A for ~0.6 ms every 4.6 ms, and a
  dip below ~3.4 V at VBAT resets the modem. The SIMCom datasheet calls for
  ≥100 µF low-ESR at VBAT; the BIGGA board carries 820 µF 6.3 V there, so
  the modem itself is covered. What remains is the 5 V input to the board's
  regulator sagging over wire and contact resistance: 100 µF plus a ceramic
  at the board's 5V/GND pins and short, thick wires take care of that. The
  1000 µF is only a fallback if the boot line ever shows BROWNOUT.
- The shield is not a UPS: expect a ~0.5 s dropout when the charger is
  plugged in. Not a problem for a tracker that persists state in NVS.
- A reversed cell burns the shield's protection IC. Check polarity before
  inserting.

### Current budget (5 V rail, typical, to be measured on the bench)

| Load | Idle | Active |
| --- | --- | --- |
| ESP32-S3 (no radio, 160 MHz) | ~40 mA | ~80 mA |
| GNSS module | ~30 mA | ~45 mA (acquisition) |
| SIM800C | ~20 mA (registered, idle) | 100–300 mA average on GPRS, 2 A bursts |
| OLED | ~10 mA | ~20 mA (all pixels) |
| SD card | ~1 mA | 50–150 mA (write) |
| **Total** | **~100 mA** | **~300–500 mA** |

At ~22 Wh of cells and ~90 % boost efficiency, roughly 2 W average gives
about 10 hours of continuous tracking; duty-cycling the modem and sleeping
when the BMA400 reports no motion is what stretches that to days. The
grading checklist only credits power claims backed by multimeter numbers,
so measure idle, tracking and uploading currents separately.

## 3. GPIO map

Pins common to both boards use GPIO1–13 (present on every SuperMini variant
and on the DevKitC). Only the GSM UART and the status LED differ.

| Function | SuperMini | DevKitC-1 | Notes |
| --- | --- | --- | --- |
| Battery voltage (ADC1_CH0) | GPIO1 | GPIO1 | Divider 100 k / 100 k from the shield's B+ (cell voltage, ≤ 4.2 V → ≤ 2.1 V). 12 dB attenuation |
| Button (wake + UI) | GPIO2 | GPIO2 | To GND, internal pull-up, GPIO interrupt → queue; RTC GPIO so it can wake deep sleep |
| GNSS UART1 RX | GPIO4 | GPIO4 | ← GNSS TX |
| GNSS UART1 TX | GPIO5 | GPIO5 | → GNSS RX (only needed to send UBX/NMEA config) |
| BMA400 INT1 | GPIO6 | GPIO6 | Motion interrupt; RTC GPIO for deep-sleep wake (EXT1) |
| BIGGA BOOT (= PWRKEY) | GPIO7 | GPIO7 | Open-drain, held low ≥ 1 s to toggle the modem's power. The board has no auto power-on, so the firmware pulses it when the modem is silent |
| I2C SDA | GPIO8 | GPIO8 | OLED 0x3C, BMA400 0x14 (0x15 if SDO high), GNSS compass 0x0D (QMC5883L) |
| I2C SCL | GPIO9 | GPIO9 | 100 kHz on breadboard; modules carry their own pull-ups |
| SD CS | GPIO10 | GPIO10 | SPI2 (FSPI) IOMUX pins, same as module 4.4 |
| SD MOSI | GPIO11 | GPIO11 | |
| SD SCK | GPIO12 | GPIO12 | |
| SD MISO | GPIO13 | GPIO13 | |
| GSM UART2 TX | GPIO43 | GPIO17 | → BIGGA RX. On the SuperMini 43/44 are the unused UART0 pins; on the DevKitC they are wired to the CP2102, so the GSM moves to 17/18 |
| GSM UART2 RX | GPIO44 | GPIO18 | ← BIGGA TX **through a 6.8 kΩ to GND**: the board's TX idles at VCC (5 V) via its 4.7 k pull-up; the divider brings it to ~3.0 V |
| Status LED (WS2812) | GPIO48 | GPIO38 | Onboard RGB. DevKitC-1 v1.0 boards use GPIO48 instead of 38 |
| Console | USB Serial/JTAG (`sdkconfig.defaults.supermini`) | UART0 via CP2102, connector labelled "UART" | On the DevKitC the "USB" connector also carries the log as secondary console |

Pins deliberately unused: GPIO0 (BOOT), GPIO3 (JTAG strapping), GPIO19/20
(USB D−/D+), GPIO26–32 (flash), GPIO45/46 (strapping).

Optional: the GNSS PPS output is a clean 1 Hz hardware event for checklist
item 2.5. The SuperMini has no spare header pin for it; on the DevKitC wire
it to GPIO16 if wanted.

## 4. Bus map

| Bus | Devices | Owner task | Protection |
| --- | --- | --- | --- |
| UART1 (GNSS) | Foxeer M10Q-250 or BE-220 | `gps_rx` | Single owner, no mutex |
| UART2 (GSM) | SIM800C | `uplink` | Single owner, no mutex |
| I2C0 | SSD1306, BMA400, compass | `display` writes, `motion` reads | One mutex for the bus |
| SPI2 | MicroSD | `logger` | One mutex for the card (also guards remount) |
| USB Serial/JTAG | Console / ESP_LOG | any task | ESP_LOG's own lock |

## 5. Logic levels and wiring risks

- Both GNSS modules output 3.3 V logic even when fed 5 V. Wire TX/RX
  directly.
- **BIGGA GSM logic level follows VCC.** Its MOSFET level shifter pulls the
  high side of TX and RX up to VCC (4.7 k on TX, 47 k on RX), so with 5 V
  in, the board's TX idles at 5 V. The ESP32-S3 is not 5 V tolerant. One
  6.8 kΩ resistor from the board's TX pin to GND forms a divider with the
  4.7 k pull-up: high level 5 × 6.8 / 11.5 ≈ 2.96 V, inside the ESP's
  2.5–3.6 V window. RX needs nothing: the ESP drives it, and the 47 k
  pull-up only sources ~36 µA. Do not feed VCC with 9 V (seller warning,
  same reason) and do not try 3.3 V (the regulator needs ≥ 4 V in).
- BIGGA header, from the schematic: 1 GND, 2 TX, 3 RX, 4 BOOT (= PWRKEY),
  5 VCC, 6 NET (NETLIGHT output), 7 DTR, 8 RING, 9 MICP, 10 MICN, 11 SPKP,
  12 SPKN. Wired: GND, TX, RX, BOOT, VCC, NET (LED). Nano-SIM holder, full-size SMA.
- VBAT on the board is 3.64 V (MIC29302, 9.09 k / 4.7 k). That leaves
  ~0.25 V to the modem's 3.4 V reset threshold, so the 5 V input must not
  sag below ~4 V during bursts: short thick wires and the 100 µF + ceramic
  at VCC/GND.
- The board may ship fixed at 9600 baud or in autobaud mode; the firmware
  probes 115200, 9600, 57600 and 38400 and then pins the modem to 115200
  with `AT+IPR`, so either works.
- SIM800C is 2G only. Confirm the SIM's operator still runs GSM/GPRS where
  the tracker will be used, and that the SIM has no PIN or that the firmware
  sends it.
- Keep the GSM antenna cable away from the GNSS patch antenna; GSM bursts
  desensitise the receiver. Mount the GNSS antenna face-up with sky view.
- The GNSS modules' default baud rates differ (Foxeer 115200, BE-220 9600).
  `Config::gpsBaudRate` must match the module in use; the firmware can later
  autodetect by trying both.
- The BMA400 is optional until it arrives: without it the firmware derives
  motion from GNSS speed and never deep-sleeps.
- SuperMini quirks: some units have the Wi-Fi antenna mis-mounted (not
  relevant, no Wi-Fi here); a crashed or deep-sleeping board can drop the
  USB port, so hold BOOT while plugging in to re-enter the bootloader.

## 6. Still to decide

- [ ] Which SIM800C breakout exactly (PWRKEY exposed? level shifter? input range) — photograph the silkscreen.
- [ ] Which SD module type (bare 3.3 V or 5 V with level shifter).
- [ ] Upload target: Traccar server (OsmAnd HTTP protocol) is the least work; a Cloudflare Worker endpoint is the alternative.
- [ ] Measure the shield's 3 V rail.
- [ ] Measure idle / tracking / uploading current for the README.

## Sources

- SuperMini pinout and quirks: [espboards.dev](https://espboards.dev/boards/esp32-s3-supermini)
- Foxeer M10Q-250: [GetFPV](https://www.getfpv.com/foxeer-m10q-250-gps-w-5883-compass.html), [RaceDayQuads](https://www.racedayquads.com/products/foxeer-gps-m10q-250-gps-compass), [FlyingTech (V2, IST8310)](https://www.flyingtech.co.uk/electronics/foxeer-m10q-250-gps-q5883-compass-module)
- Beitian BE-220 / BN-220: [UnmannedTech](https://www.unmannedtechshop.co.uk/products/beitian-bn-220-gps-module), [Tokopedia BE-220 listing](https://www.tokopedia.com/buayaaerotech/gps-beitian-be-220-be220-gnss-module-for-rc-drone)
- SIM800C supply and bursts: [SIM800C datasheet (alldatasheet mirror)](https://htmlapp.alldatasheet.com/html-pdf/1741387/SIMCOM/SIM800C/10571/41/SIM800C.html), [5V/3.3V TTL breakout](https://st.robu.in/?p=1794261)
- Battery Shield V8: [Zbotic](https://zbotic.in/?p=453818), [ShillehTek manual](https://shillehtek.com/blogs/shillehtek-product-manuals/2x18650-battery-shield-v8-5v-3a-esp32-arduino-manual)
- BMA400: [SparkFun hookup guide](https://learn.sparkfun.com/tutorials/sparkfun-triple-axis-accelerometer-breakout---bma400-qwiic-hookup-guide), [ESP-IDF component](https://components.espressif.com/components/solderedelectronics/soldered-bma400-esp-idf-component/versions/1.0.0/readme)
