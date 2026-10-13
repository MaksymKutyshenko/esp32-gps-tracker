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
| BMA400 (CJMCU-400) | Motion detection in hardware: its activity interrupt drives the moving/parked state (no fix needed, immune to GNSS noise) and will wake the tracker from deep sleep. Optional: without it motion comes from GNSS speed | I2C 0x14/0x15, INT1 → GPIO6 | 3.3 V only |
| SSD1306 0.96" OLED | Status display: fix state, satellites, speed, coordinates, battery, upload state | I2C 0x3C | 3.3 V |
| MicroSD adapter (6-pin, 4.5–5.5 V) + card | Track log (CSV) and upload backlog while offline | SPI (with DMA) | 5 V on VCC; onboard regulator and level shifter give 3.3 V signals |
| Battery Shield V8 + 2×18650 HG2 | Power: cells in parallel (≈6000 mAh), 5 V boost output (≈2.2 A continuous, 3 A peak), 3 V/3.3 V 1 A rail, charging at 0.6–0.8 A | — | — |
| 6.8 kΩ resistor | From BIGGA TX to GND: level divider for the 5 V TX line (see §5) | — | — |
| Red LED + 1 kΩ | On BIGGA NET (NETLIGHT): blink rate shows searching / registered / GPRS without any firmware | — | — |

## 2. Power tree

```
2×18650 HG2 ──► Battery Shield V8 ──► 5V rail ──┬─► ESP32-S3 SuperMini (5V pin → onboard LDO → 3V3)
   (parallel)   (boost + protection)             ├─► BIGGA GSM VCC (board has 100+10+1 µF on VCC, ~600–900 µF on VBAT)
                                                 ├─► Foxeer M10Q-250 (5V in)  /  BE-220 (3.0–5.5 V)
                                                 └─► MicroSD adapter VCC (5 V type: regulator + level shifter on board)
                                      3V rail ───┬─► SSD1306 OLED
                                      (1 A)      ├─► BMA400
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
  the modem itself is covered, and its VCC input already has 100 + 10 + 1 µF
  at the pins, so no external capacitor is fitted. What remains is wire and
  contact resistance between the shield and the board: short, thick wires.
  The 1000 µF is only a fallback across VCC/GND if the boot line ever shows
  BROWNOUT.
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
| GNSS UART1 RX | GPIO5 | GPIO5 | ← GNSS TX |
| GNSS UART1 TX | GPIO4 | GPIO4 | → GNSS RX (sends the UBX setup at boot) |
| BMA400 INT1 | GPIO6 | GPIO6 | Motion interrupt; RTC GPIO for deep-sleep wake (EXT1) |
| BIGGA BOOT (= PWRKEY) | GPIO7 | GPIO7 | Open-drain, held low ≥ 1 s to toggle the modem's power. The board has no auto power-on, so the firmware pulses it when the modem is silent |
| I2C SDA | GPIO8 | GPIO8 | OLED 0x3C, BMA400 0x14 (0x15 if SDO high), GNSS compass 0x0D (QMC5883L) |
| I2C SCL | GPIO9 | GPIO9 | 100 kHz on breadboard; modules carry their own pull-ups |
| SD CS | GPIO10 | GPIO10 | SPI2 through the GPIO matrix (SCK/MOSI swapped from the IOMUX pins for a straight 10-11-12-13 run; fine at 10 MHz) |
| SD SCK | GPIO11 | GPIO11 | |
| SD MOSI | GPIO12 | GPIO12 | |
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
- BIGGA header. Schematic: 1 GND, 2 TX, 3 RX, 4 BOOT (= PWRKEY), 5 VCC,
  6 NET (NETLIGHT output), 7 DTR, 8 RING, 9 MICP, 10 MICN, 11 SPKP, 12 SPKN.
  Silkscreen on the delivered board: `NET VSS PK RX TX GND` on one edge
  (the same six pins read from the other end; **VSS is the 5 V supply
  input, not ground**, and PK is the power key) and `SPN SPP MICN MICP RING
  DTR` on the other. Wired: GND, TX, RX, PK, VSS, NET (LED). Nano-SIM
  holder, full-size SMA.
- VBAT on the board is 3.64 V (MIC29302, 9.09 k / 4.7 k). That leaves
  ~0.25 V to the modem's 3.4 V reset threshold, so the 5 V input must not
  sag below ~4 V during bursts: short thick wires from the shield.
- The board may ship fixed at 9600 baud or in autobaud mode; the firmware
  probes 115200, 9600, 57600 and 38400 and then pins the modem to 115200
  with `AT+IPR`, so either works.
- SIM800C is 2G only. Confirm the SIM's operator still runs GSM/GPRS where
  the tracker will be used, and that the SIM has no PIN or that the firmware
  sends it.
- Keep the GSM antenna cable away from the GNSS patch antenna; GSM bursts
  desensitise the receiver. Mount the GNSS antenna face-up with sky view.
  Indoors the receiver reports satellites with a signal but none used
  (`0/2 sat`, `SEARCH` on the screen); that is the antenna, not the
  firmware. The system page's `sky N sig M cn0 X` line separates the two
  failures: `sig 0 cn0 0` with a non-zero `sky` is an antenna that
  delivers nothing (cable, connector, placement), `sig` > 0 with `cn0`
  below ~30 is a signal too weak to decode (move to the window or out). The two worst neighbours on the bench are the SSD1306 (its
  charge pump radiates right into the L1 band) and the devkit with its
  USB cable: keep the patch at least 10–15 cm from both, on the window
  side. A cold start wants 30–60 s of clear sky; near a window it can take
  several minutes.
- At boot the firmware sets the receiver to 1 Hz with RMC, GGA and GSV only
  (UBX-CFG-VALSET on the M10, CFG-RATE/CFG-MSG on the M8), RAM layer only,
  so a power cycle restores the module's own settings. Untouched, the
  Foxeer streams 120 sentences a second, most of the UART's capacity.
  The same sequence enables the receiver's static hold (speed below
  1 m/s: report 0 and freeze the position until it moves 20 m) and its
  accuracy mask (a fix whose own horizontal accuracy estimate is worse
  than 30 m is reported as invalid). Without them a parked tracker at a
  window shows 1-3 km/h and a position that jumps 50 m at a time, and
  every jump would be recorded and drawn on the map as a walk. The
  firmware adds its own gate in `LogPolicy`: at least 5 satellites in
  the solution and HDOP ≤ 3.0, or the fix is shown but not recorded.
- The GNSS modules' default baud rates differ (Foxeer 115200, BE-220 9600).
  `Config::gpsBaudRate` must match the module in use; the firmware can later
  autodetect by trying both.
- CJMCU-400 wiring: VCC to a **3.3 V** rail (never 5 V), GND, SDA to
  GPIO8, SCL to GPIO9, INT1 to GPIO6. CS must be high for I2C; the board
  pulls it up, tie it to 3.3 V if the chip stays silent. SDO selects the
  address (low 0x14, high 0x15) and the firmware probes both, so it can be
  left as the board sets it. On the bench the chip answered at 0x15 on
  only two boots in three with CS left to the board: tie CS to 3.3 V.
  The firmware retries the probe three times and, if the chip is still
  silent, runs without deep sleep for that boot (nothing else could wake
  it on the road). INT2 unused. The boot log reports
  `BMA400 at 0x14: activity > 96 mg for 10 samples` or `No BMA400 ...;
  motion from GNSS speed`; the diagnostics line shows `motion bma400
  pulses N`, and `pulses` should stay flat while the tracker lies still
  and climb when it is picked up.
- Without the BMA400 the firmware derives motion from GNSS speed and
  never deep-sleeps.
- Deep sleep and the shield: asleep, the board still draws the shield's
  converter quiescent current plus ~1 mA for the SD adapter, ~2 mA for
  the detached modem, 20 µA for the GNSS in backup mode and 10 µA each for
  the ESP32, OLED and BMA400. Many 18650 shields cut their 5 V output
  when the load falls below a few tens of mA, power-bank style, and a
  sleeping tracker would then lose power and never wake. This shield
  keeps its output up at the sleeping load (checked 2026-10-13: still
  powered after an hour asleep). Wake
  sources are INT1 (GPIO6, high) and the button (GPIO2, low); both are
  RTC-capable pins, which is why they were chosen.
- 5 V pins differ between the two boards. The devkit's `5VIN` sits behind
  a diode: input only, reads ~1.7 V of leakage on a meter, cannot feed a
  module. The SuperMini's `5V` is the USB VBUS line itself: a real 5 V
  output on USB power and the supply input from the shield otherwise, so
  the shield must never be on that pin while a USB cable is plugged in.
- Breadboard power: the modem's VSS and GND get their own two wires to
  shield pins, twisted; the devkit's GND gets its own wire to another
  shield GND pin. The modem's 2 A transmit pulses through a shared rail
  bounce the common ground by ~200 mV and corrupt every bus at once
  (seen on the bench: UART bytes, I2C timeouts, lost tick interrupt,
  watchdog resets). Bisected on 2026-10-11: the resets follow a modem
  power-on, i.e. the registration burst, when the modem transmits at
  full power until the network backs it off; steady traffic afterwards
  is survived. The firmware therefore power-cycles the modem only after
  unanswered requests, never after a server rejection. The wiring test
  is the console command `at AT+CFUN=1,1`, which forces a fresh
  registration: three in a row without a reset means the supply and
  ground are good enough. If it resets, the next steps in order are the
  antenna on the SMA, 1000 µF + 100 nF at the modem's VSS/GND pins, and
  distance between the modem and the devkit. A cold boot is the heavier
  case (SIM init and band scan at full power) and is forced with
  `at AT+CPOWD=1`: the modem switches off, three uploads time out and the
  firmware pulses PWRKEY. Result on 2026-10-11 with the antenna on the
  SMA, modem VSS/GND on their own shield pins and the devkit GND on its
  own wire: 3/3 radio restarts and 3/3 cold boots without a reset, nine
  minutes of uptime across three power cycles. The same two commands
  are the acceptance test for the SuperMini build. That passed with the
  laptop attached; on battery alone the devkit hangs off the shield's
  converter together with the modem and the resets came back (one boot
  every 30 s, during the first upload). The **1000 µF electrolytic across
  the modem's VSS and GND pins** ended them: the next run went 648 s to
  its scheduled sleep and the sleep / wake cycles after it had no resets
  at all. It is part of the design, not a bench aid.
- Battery Shield V8 has no seamless power path: plugging or unplugging
  the charger switches the boost converter between the input and the
  cells, and the 5 V / 3 V outputs dip for tens of milliseconds. The
  ESP32 browns out or power-on resets (`Boot: reset reason power-on`),
  the OLED goes black for a moment, the modem rides through on its own
  capacitors or restarts. On the bench pulling the charger out reset the
  chip; plugging it in did not.
  Plug and unplug the charger with the tracker off, or hold the 5 V rail
  up with a 1000 µF capacitor near the devkit.
- The card in motion: on the first walk (2026-10-13) the tracker took
  nine task-watchdog resets, all while moving, none at the bench. The
  card's five jumper wires are the suspect: a write that stalls because a
  contact bounces keeps the logger task inside the SD driver, and
  ESP-IDF's write timeout (5 s) is the watchdog period. The firmware now
  caps card commands at 1 s (`Config::sdCommandTimeoutMs`) and names the
  task behind a watchdog reset at the next boot, so the next walk tells.
  Seat the card wires firmly, keep them short, and keep 10 MHz.
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
