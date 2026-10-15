#!/usr/bin/env python3
"""Generates hardware/kicad/gps_tracker.kicad_sch from the design in hardware/README.md.

The schematic is built from KiCad's own symbol libraries (ESP32-S3-WROOM-1
module, generic pin-header connectors for the breakout modules, passives,
power symbols). Every connection is a net label placed on a short stub at
the pin, so the drawing stays readable and the GPIO map in config.hpp is
the single source for the pin assignment.

Run from the project root:
    python3 tools/gen_schematic.py
    kicad-cli sch erc hardware/kicad/gps_tracker.kicad_sch
    kicad-cli sch export pdf hardware/kicad/gps_tracker.kicad_sch -o hardware/kicad/gps_tracker.pdf
"""
import math
import re
import uuid
from pathlib import Path

LIB_DIR = Path("/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols")
OUT_DIR = Path("hardware/kicad")
PROJECT = "gps_tracker"

# --------------------------------------------------------------------------
# Minimal s-expression handling: we only need to cut a symbol block out of a
# library file and read its pins, so a bracket-depth scan is enough.


def extract_symbol(lib: str, name: str) -> str:
    text = (LIB_DIR / f"{lib}.kicad_sym").read_text()
    needle = f'(symbol "{name}"'
    start = text.find(needle)
    if start < 0:
        raise SystemExit(f"{lib}:{name} not found")
    depth = 0
    i = start
    in_string = False
    while i < len(text):
        c = text[i]
        if c == '"' and text[i - 1] != "\\":
            in_string = not in_string
        elif not in_string:
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
                if depth == 0:
                    return text[start : i + 1]
        i += 1
    raise SystemExit("unbalanced symbol block")


PIN_RE = re.compile(
    r'\(pin (\w+) \w+\s*\(at ([-\d.]+) ([-\d.]+) (\d+)\)\s*\(length ([\d.]+)\).*?'
    r'\(name "([^"]*)".*?\(number "([^"]+)"',
    re.S,
)


def symbol_pins(block: str):
    """number -> (x, y, angle, name, electrical type) in library coordinates (y up)."""
    pins = {}
    for etype, x, y, angle, _length, name, number in PIN_RE.findall(block):
        pins[number] = (float(x), float(y), int(angle), name, etype)
    return pins


class Library:
    def __init__(self):
        self.blocks = {}
        self.pins = {}

    def use(self, lib: str, name: str) -> str:
        key = f"{lib}:{name}"
        if key not in self.blocks:
            block = extract_symbol(lib, name)
            block = block.replace(f'(symbol "{name}"', f'(symbol "{key}"', 1)
            self.blocks[key] = block
            self.pins[key] = symbol_pins(block)
        return key


# --------------------------------------------------------------------------
# Schematic items


def uid() -> str:
    return str(uuid.uuid4())


ROOT_UUID = uid()
items = []


def fmt(v: float) -> str:
    return f"{v:.2f}".rstrip("0").rstrip(".")


def q(text: str) -> str:
    """Escapes a string for a KiCad quoted field."""
    return text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")


def outward(angle: int):
    """Unit vector pointing away from the symbol body, in schematic coords (y down)."""
    rad = math.radians(angle)
    return (-round(math.cos(rad)), round(math.sin(rad)))


class Part:
    def __init__(self, lib: Library, lib_id: str, ref: str, value: str, x: float, y: float,
                 value_offset=(0, 0), ref_offset=(0, 0)):
        self.lib_id = lib_id
        self.ref = ref
        self.value = value
        # KiCad connects only on its 1.27 mm grid (50 mil); snap every origin.
        self.x = round(x / 1.27) * 1.27
        self.y = round(y / 1.27) * 1.27
        self.pins = lib.pins[lib_id]
        self.used = set()
        self.value_offset = value_offset
        self.ref_offset = ref_offset

    def pin_point(self, number: str):
        px, py, angle, _, _ = self.pins[number]
        return (self.x + px, self.y - py), angle

    def emit(self):
        rx, ry = self.ref_offset
        vx, vy = self.value_offset
        props = [
            ("Reference", self.ref, self.x + rx, self.y + ry, self.ref.startswith("#")),
            ("Value", self.value, self.x + vx, self.y + vy, False),
            ("Footprint", "", self.x, self.y, True),
            ("Datasheet", "~", self.x, self.y, True),
            ("Description", "", self.x, self.y, True),
        ]
        out = [f'  (symbol (lib_id "{self.lib_id}") (at {fmt(self.x)} {fmt(self.y)} 0) (unit 1)',
               '    (exclude_from_sim no) (in_bom yes) (on_board yes) (dnp no)',
               f'    (uuid "{uid()}")']
        for name, val, px, py, hide in props:
            hide_s = " (hide yes)" if hide else ""
            out.append(f'    (property "{name}" "{q(val)}" (at {fmt(px)} {fmt(py)} 0) '
                       f'(effects (font (size 1.27 1.27)) (justify left){hide_s}))')
        for number in self.pins:
            out.append(f'    (pin "{number}" (uuid "{uid()}"))')
        out.append(f'    (instances (project "{PROJECT}" (path "/{ROOT_UUID}" '
                   f'(reference "{self.ref}") (unit 1))))')
        out.append("  )")
        items.append("\n".join(out))


def wire(p1, p2):
    items.append(f'  (wire (pts (xy {fmt(p1[0])} {fmt(p1[1])}) (xy {fmt(p2[0])} {fmt(p2[1])})) '
                 f'(stroke (width 0) (type default)) (uuid "{uid()}"))')


def label(name: str, point, direction):
    dx, dy = direction
    if dx < 0:
        rot, justify = 180, "right bottom"
    elif dx > 0:
        rot, justify = 0, "left bottom"
    elif dy < 0:
        rot, justify = 90, "left bottom"
    else:
        rot, justify = 270, "right bottom"
    items.append(f'  (label "{q(name)}" (at {fmt(point[0])} {fmt(point[1])} {rot}) '
                 f'(effects (font (size 1.27 1.27)) (justify {justify})) (uuid "{uid()}"))')


def no_connect(point):
    items.append(f'  (no_connect (at {fmt(point[0])} {fmt(point[1])}) (uuid "{uid()}"))')


def text(body: str, x: float, y: float, size=1.27):
    body = q(body)
    items.append(f'  (text "{body}" (exclude_from_sim no) (at {fmt(x)} {fmt(y)} 0) '
                 f'(effects (font (size {size} {size})) (justify left bottom)) (uuid "{uid()}"))')


STUB = 5.08


def connect(part: Part, number: str, net: str, lib: Library = None, power: str = None):
    """Stub out of the pin, then a net label or a power symbol."""
    point, angle = part.pin_point(number)
    d = outward(angle)
    end = (point[0] + d[0] * STUB, point[1] + d[1] * STUB)
    wire(point, end)
    part.used.add(number)
    if power:
        p = Part(lib, power, f"#PWR{len(items):03d}", power.split(":")[1], end[0], end[1])
        # Hide reference/value clutter for power symbols: KiCad shows the value only.
        p.ref_offset = (0, 0)
        p.value_offset = (d[0] * 2.54 + 1.27, d[1] * 5.08 - (0 if d[1] else 1.5))
        p.emit()
    else:
        label(net, end, d)


def finish_unused(part: Part):
    for number in part.pins:
        if number not in part.used:
            point, _ = part.pin_point(number)
            no_connect(point)


# --------------------------------------------------------------------------
# The design (pins from firmware/include/config.hpp, SuperMini environment)

lib = Library()
WROOM = lib.use("RF_Module", "ESP32-S3-WROOM-1")
C4 = lib.use("Connector_Generic", "Conn_01x04")
C6 = lib.use("Connector_Generic", "Conn_01x06")
C8 = lib.use("Connector_Generic", "Conn_01x08")
C12 = lib.use("Connector_Generic", "Conn_01x12")
R = lib.use("Device", "R")
LED = lib.use("Device", "LED")
CPOL = lib.use("Device", "C_Polarized")
QNPN = lib.use("Transistor_BJT", "Q_NPN_EBC")
C3 = lib.use("Connector_Generic", "Conn_01x03")
P5V = lib.use("power", "+5V")
P3V3 = lib.use("power", "+3V3")
PBAT = lib.use("power", "+BATT")
PGND = lib.use("power", "GND")
PFLAG = lib.use("power", "PWR_FLAG")

# --- ESP32-S3 (SuperMini board; WROOM-1 symbol for the pinout) -----------
u1 = Part(lib, WROOM, "U1", "ESP32-S3 SuperMini (ESP32-S3FH4R2)", 200, 130,
          ref_offset=(-18, -40), value_offset=(-18, -37))
u1.emit()
gpio = {
    "IO1": "BATT_SENSE", "IO2": "BTN", "IO4": "GNSS_RXD", "IO5": "GNSS_TXD",
    "IO6": "ACC_INT1", "IO7": "PWRKEY_GPIO", "IO8": "SDA", "IO9": "SCL",
    "IO10": "SD_CS", "IO11": "SD_SCK", "IO12": "SD_MOSI", "IO13": "SD_MISO",
    "TXD0": "GSM_RXD", "RXD0": "GSM_TXD",   # IO43 / IO44: UART2 on the SuperMini
}
by_name = {v[3]: k for k, v in u1.pins.items()}
for name, net in gpio.items():
    connect(u1, by_name[name], net)
connect(u1, "1", "", lib, PGND)
u1.used.update({"40", "41"})  # stacked on pin 1 in the library symbol
connect(u1, "2", "3V3_BOARD")    # the board's LDO output: NOT tied to the shield's rail
finish_unused(u1)
text("SuperMini: 5V pin = its USB VBUS (no diode): +5V from the shield only with USB unplugged.\n"
     "Onboard LDO makes 3V3_BOARD. USB (console), EN, BOOT and the RGB LED (IO48) stay on the board.\n"
     "DevKitC-1 build: GSM on IO17/IO18 instead of IO43/IO44.",
     176, 182, 1.27)

# --- GNSS module -----------------------------------------------------------
j1 = Part(lib, C6, "J1", "GNSS: Foxeer M10Q-250 / Beitian BE-220", 60, 40,
          ref_offset=(-4, -10), value_offset=(-4, -8))
j1.emit()
connect(j1, "1", "+5V")
connect(j1, "2", "GND")
connect(j1, "3", "GNSS_TXD")
connect(j1, "4", "GNSS_RXD")
connect(j1, "5", "SCL")
connect(j1, "6", "SDA")
text("1 VCC 5V   2 GND   3 TX -> IO5   4 RX <- IO4\n5 SCL / 6 SDA: compass (Foxeer only, 0x0D). BE-220 has pins 1-4.",
     40, 62, 1.27)

# --- BIGGA GSM (SIM800C) board, header per its schematic ----------------
j2 = Part(lib, C12, "J2", "BIGGA GSM (SIM800C) board", 60, 100, ref_offset=(-4, -18), value_offset=(-4, -16))
j2.emit()
connect(j2, "1", "GND")
connect(j2, "2", "GSM_TXD")       # idles at VCC (5 V) on the board: R3 below divides it
connect(j2, "3", "GSM_RXD")
connect(j2, "4", "GSM_PWRKEY")    # "BOOT" on the silkscreen = SIM800C PWRKEY; Q1 below
connect(j2, "5", "+5V")
connect(j2, "6", "GSM_NET")
finish_unused(j2)                 # DTR, RING, audio
text("Silkscreen: GND  TX (5 V level!) -> IO44  RX <- IO43  PK = BOOT/PWRKEY  VSS = VCC 5V in  NET\n"
     "6 NET -> R4 1k -> LED1 (NETLIGHT: 0.8 s searching, 3 s registered, 0.3 s GPRS)  7 DTR  8 RING  9-12 audio: unused.\n"
     "TX high side is pulled to VCC through 4.7k on the board: R3 divides it to ~3.0 V for IO44.\n"
     "PK <- Q1 2N2222A collector; IO7 -> R5 10k -> base, R6 100k base to GND: IO7 high = key pressed.\n"
     "VBAT is 3.64 V (MIC29302). C1 1000 uF across VSS/GND right at the board's pins (+ to VSS): the onboard\n"
     "100+10+1 uF are not enough on battery (reset every 30 s during registration without it). Short thick wires.",
     40, 142, 1.27)
# C1: bulk capacitor on the modem board's 5 V input, as close to its pins
# as the wires allow. Pin 1 is the positive plate.
c1 = Part(lib, CPOL, "C1", "1000u", 136, 92, ref_offset=(3, -2), value_offset=(3, 1))
c1.emit()
connect(c1, "1", "+5V")
connect(c1, "2", "", lib, PGND)
# R3: divider against the board's 4.7k pull-up; the ESP side is GSM_TXD.
r3 = Part(lib, R, "R3", "6.8k", 100, 92, ref_offset=(2, -2), value_offset=(2, 1))
r3.emit()
connect(r3, "1", "GSM_TXD")   # same net as J2 pin 2 and IO44: the divider hangs off it
connect(r3, "2", "", lib, PGND)
# NETLIGHT indicator: 2.8 V output from the modem, ~1 mA through a red LED.
r4 = Part(lib, R, "R4", "1k", 118, 92, ref_offset=(2, -2), value_offset=(2, 1))
r4.emit()
connect(r4, "1", "GSM_NET")
# Horizontal symbol: anode (pin 2) on the right lands under R4 pin 2.
led1 = Part(lib, LED, "LED1", "red NET", 114.3, 102, ref_offset=(-2, -4), value_offset=(-4, 5))
led1.emit()
p_r4, _ = r4.pin_point("2")
p_led, _ = led1.pin_point("2")   # anode (pin 2 on Device:LED) up to the resistor
wire(p_r4, p_led)
r4.used.add("2")
led1.used.add("2")
connect(led1, "1", "", lib, PGND)

# PWRKEY through an NPN: IO7 drives only the base (R5), R6 holds the base
# low while the ESP boots or sleeps, the collector pulls PWRKEY down. No wire
# from the modem reaches the ESP pin; a bare wire there carried every attach
# burst into the chip. The drive is inverted: IO7 high = key pressed.
r5 = Part(lib, R, "R5", "10k", 154, 92, ref_offset=(2, -2), value_offset=(2, 1))
r5.emit()
connect(r5, "1", "PWRKEY_GPIO")
connect(r5, "2", "PWRKEY_BASE")
r6 = Part(lib, R, "R6", "100k", 166, 92, ref_offset=(2, -2), value_offset=(2, 1))
r6.emit()
connect(r6, "1", "PWRKEY_BASE")
connect(r6, "2", "", lib, PGND)
q1 = Part(lib, QNPN, "Q1", "2N2222A", 160, 120, ref_offset=(-10, 4), value_offset=(-10, 7))
q1.emit()
connect(q1, "1", "", lib, PGND)      # E
connect(q1, "2", "PWRKEY_BASE")      # B
connect(q1, "3", "GSM_PWRKEY")       # C

# --- OLED ------------------------------------------------------------------
j3 = Part(lib, C4, "J3", "SSD1306 0.96\" OLED I2C 0x3C", 320, 40, ref_offset=(-4, -8), value_offset=(-4, -6))
j3.emit()
connect(j3, "1", "GND")
connect(j3, "2", "+3V3")
connect(j3, "3", "SCL")
connect(j3, "4", "SDA")
text("1 GND  2 VCC 3V3  3 SCL <- IO9  4 SDA <> IO8\nModule pull-ups on SDA/SCL; bus at 100 kHz.", 300, 56, 1.27)

# --- MicroSD ---------------------------------------------------------------
j4 = Part(lib, C6, "J4", "MicroSD SPI module", 320, 85, ref_offset=(-4, -10), value_offset=(-4, -8))
j4.emit()
connect(j4, "1", "GND")
connect(j4, "2", "+5V")
connect(j4, "3", "SD_MISO")
connect(j4, "4", "SD_MOSI")
connect(j4, "5", "SD_SCK")
connect(j4, "6", "SD_CS")
text("1 GND  2 VCC 5V (adapter has its own 3.3 V regulator and level shifter)\n"
     "3 MISO -> IO13  4 MOSI <- IO12  5 SCK <- IO11  6 CS <- IO10", 300, 107, 1.27)

# --- BMA400 ---------------------------------------------------------------
j6 = Part(lib, C8, "J6", "BMA400 CJMCU-400", 320, 135, ref_offset=(-4, -12), value_offset=(-4, -10))
j6.emit()
connect(j6, "1", "+3V3")
connect(j6, "2", "GND")
connect(j6, "3", "SDA")
connect(j6, "4", "SCL")
connect(j6, "5", "ACC_INT1")
# CS high at power-up selects I2C; the board's own pull-up was not enough
# (SPI mode on one boot in three). SDO low fixes the address at 0x14.
connect(j6, "7", "+3V3")
connect(j6, "8", "GND")
finish_unused(j6)  # 6 INT2 unused
text("1 VCC 3V3  2 GND  3 SDA  4 SCL  5 INT1 -> IO6 (RTC GPIO, deep-sleep wake)  6 INT2 n/c\n"
     "7 CS -> 3V3: I2C mode at power-up (floating, the chip started in SPI mode on 1 boot in 3)\n"
     "8 SDO -> GND: address 0x14 (0x15 with SDO high; the firmware probes both).",
     300, 162, 1.27)

# --- Battery shield --------------------------------------------------------
j5 = Part(lib, C4, "J5", "Battery Shield V8, 2x18650", 60, 160, ref_offset=(-4, -8), value_offset=(-4, -6))
j5.emit()
connect(j5, "1", "+5V")
connect(j5, "2", "+3V3")
connect(j5, "3", "GND")
connect(j5, "4", "+BATT")
text("1 5V out (<= 2.2 A cont.)  2 3V out (1 A; measure: 3.0 or 3.3 V)  3 GND  4 B+ (cell)\n"
     "Feeds: +5V -> SuperMini 5V pin (USB unplugged), GNSS, SIM800C board, SD. +3V3 -> OLED, BMA400, button.",
     40, 176, 1.27)

# --- Battery sense divider -------------------------------------------------
r1 = Part(lib, R, "R1", "100k", 150, 160, ref_offset=(2, -2), value_offset=(2, 1))
r1.emit()
r2 = Part(lib, R, "R2", "100k", 150, 175, ref_offset=(2, -2), value_offset=(2, 1))
r2.emit()
connect(r1, "1", "", lib, PBAT)
connect(r2, "2", "", lib, PGND)
# r1 pin 2 and r2 pin 1 meet at the midpoint; wire them and label the node.
p_r1, _ = r1.pin_point("2")
p_r2, _ = r2.pin_point("1")
wire(p_r1, p_r2)
wire(p_r1, (p_r1[0] + 7.62, p_r1[1]))
label("BATT_SENSE", (p_r1[0] + 7.62, p_r1[1]), (1, 0))
r1.used.update({"2"})
r2.used.update({"1"})
text("Cell 4.2 V max -> 2.1 V at IO1 (ADC1_CH0, 12 dB).", 140, 198, 1.27)

# --- Button module (3-pin: VCC, GND, SIG with its own pull-down) -------------
j7 = Part(lib, C3, "J7", "Button module", 120, 118, ref_offset=(-4, -8), value_offset=(-4, -6))
j7.emit()
connect(j7, "1", "+3V3")
connect(j7, "2", "GND")
connect(j7, "3", "BTN")
text("1 VCC 3V3  2 GND  3 SIG -> IO2\n"
     "Own pull-down: SIG low at rest,\n"
     "3V3 when pressed (buttonPressedHigh).", 105, 128, 1.27)

# --- Power flags so ERC knows the rails are driven by the shield -------------
for i, (sym, net_x) in enumerate([(P5V, 250), (P3V3, 265), (PBAT, 280), (PGND, 295)]):
    flag = Part(lib, PFLAG, f"#FLG{i}", "PWR_FLAG", net_x, 200)
    flag.emit()
    rail = Part(lib, sym, f"#PWR9{i}", sym.split(":")[1], net_x, 200)
    rail.value_offset = (2, -2)
    rail.emit()
flag = Part(lib, PFLAG, "#FLG4", "PWR_FLAG", 310, 200)
flag.emit()
flag_point, _ = flag.pin_point("1")
wire(flag_point, (flag_point[0], flag_point[1] + STUB))
label("3V3_BOARD", (flag_point[0], flag_point[1] + STUB), (0, 1))
text("PWR_FLAG: the rails are sourced by the battery shield (J5); 3V3_BOARD by the board's LDO.",
     245, 212, 1.27)

# --------------------------------------------------------------------------
# File assembly

header = f'''(kicad_sch (version 20250114) (generator "gen_schematic.py") (generator_version "9.0")
  (uuid "{ROOT_UUID}")
  (paper "A3")
  (title_block
    (title "GPS Tracker")
    (date "2026-10-15")
    (rev "2")
    (company "Maksym Kutyshenko")
    (comment 1 "ESP32-S3 SuperMini build; pins per firmware/include/config.hpp")
    (comment 2 "Breakout modules drawn as pin headers; see hardware/README.md")
  )
  (lib_symbols
{chr(10).join(lib.blocks.values())}
  )
'''
footer = f'''  (sheet_instances (path "/" (page "1")))
  (embedded_fonts no)
)
'''

OUT_DIR.mkdir(parents=True, exist_ok=True)
(OUT_DIR / f"{PROJECT}.kicad_sch").write_text(header + "\n".join(items) + "\n" + footer)
(OUT_DIR / f"{PROJECT}.kicad_pro").write_text(
    '{\n  "board": {},\n  "boards": [],\n  "libraries": {"pinned_footprint_libs": [], "pinned_symbol_libs": []},\n'
    f'  "meta": {{"filename": "{PROJECT}.kicad_pro", "version": 1}},\n  "sheets": [],\n  "text_variables": {{}}\n}}\n')
print(f"wrote {OUT_DIR / PROJECT}.kicad_sch with {len(items)} items")
