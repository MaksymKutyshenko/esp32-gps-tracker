#!/usr/bin/env python3
"""Draws the bench wiring diagram (SVG) for the assembly sheet.

Breadboard-style layout: power rails run as vertical buses down both
sides, the DevKitC-1 sits in the middle with its pins in logical order,
modules stand left and right. Power pins leave a module towards its rail
side, signal pins towards the devkit, so power and signal wires never
cross. Every wire is a Manhattan polyline in the colour of its signal
class. Pure geometry; run it and paste the output into the sheet.

    python3 tools/gen_wiring_svg.py > wiring.svg
"""

W, H = 1200, 775

COL = {
    "5V": "#e03a4e", "3V3": "#f29a1f", "GND": "var(--rail-gnd)", "BATT": "#9b6ff0",
    "TX": "#d9a300", "RX": "#2eaa5e", "SDA": "#3b8de0", "SCL": "#9aa3b2",
    "SPI": "#7c8696", "CTRL": "#b0733b",
}
RAIL_LABEL = {"5V": "+5V", "3V3": "+3V3", "GND": "GND", "BATT": "B+"}

out = []
def add(s): out.append(s)

def box(x, y, w, h, title, sub=None, dashed=False):
    dash = ' stroke-dasharray="6 4"' if dashed else ""
    add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="6" fill="var(--paper)" stroke="var(--fg)" stroke-width="1.5"{dash}/>')
    add(f'<text x="{x+10}" y="{y+22}" class="t-title">{title}</text>')
    if sub:
        add(f'<text x="{x+10}" y="{y+38}" class="t-sub">{sub}</text>')

def pin(x, y, label, side):
    """Pin stub leaving a box edge at (x, y): 'r' exits right, 'l' exits left."""
    if side == "r":
        add(f'<line x1="{x-22}" y1="{y}" x2="{x}" y2="{y}" stroke="var(--fg)" stroke-width="1.5"/>')
        add(f'<text x="{x-28}" y="{y+4}" class="t-pin" text-anchor="end">{label}</text>')
    else:
        add(f'<line x1="{x}" y1="{y}" x2="{x+22}" y2="{y}" stroke="var(--fg)" stroke-width="1.5"/>')
        add(f'<text x="{x+28}" y="{y+4}" class="t-pin">{label}</text>')
    add(f'<circle cx="{x}" cy="{y}" r="2.5" fill="var(--fg)"/>')

def wire(points, color, width=2.2, dashed=False):
    pts = " ".join(f"{x},{y}" for x, y in points)
    dash = ' stroke-dasharray="5 4"' if dashed else ""
    add(f'<polyline points="{pts}" fill="none" stroke="{color}" stroke-width="{width}" stroke-linejoin="round" stroke-linecap="round"{dash}/>')

def dot(x, y, color):
    add(f'<circle cx="{x}" cy="{y}" r="3.2" fill="{color}"/>')

def ground(x, y, color="var(--rail-gnd)"):
    add(f'<line x1="{x}" y1="{y}" x2="{x}" y2="{y+8}" stroke="{color}" stroke-width="2"/>')
    add(f'<line x1="{x-8}" y1="{y+8}" x2="{x+8}" y2="{y+8}" stroke="{color}" stroke-width="2"/>')
    add(f'<line x1="{x-5}" y1="{y+12}" x2="{x+5}" y2="{y+12}" stroke="{color}" stroke-width="2"/>')
    add(f'<line x1="{x-2}" y1="{y+16}" x2="{x+2}" y2="{y+16}" stroke="{color}" stroke-width="2"/>')

def part(x, y, w, h, label, vertical=False):
    if vertical:
        add(f'<rect x="{x-h/2}" y="{y-w/2}" width="{h}" height="{w}" fill="var(--paper)" stroke="var(--fg)" stroke-width="1.5"/>')
        add(f'<text x="{x+h/2+5}" y="{y+4}" class="t-part">{label}</text>')
    else:
        add(f'<rect x="{x-w/2}" y="{y-h/2}" width="{w}" height="{h}" fill="var(--paper)" stroke="var(--fg)" stroke-width="1.5"/>')
        add(f'<text x="{x}" y="{y+4}" class="t-part" text-anchor="middle">{label}</text>')

def tag(x, y, text):
    """Small pill naming the bus a pin group belongs to."""
    w = 7 * len(text) + 10
    add(f'<rect x="{x - w/2}" y="{y - 8}" width="{w}" height="16" rx="8" fill="var(--bg)" stroke="var(--line)" stroke-width="1"/>')
    add(f'<text x="{x}" y="{y + 4}" class="t-tag" text-anchor="middle">{text}</text>')

def note(x, y, text, anchor="start"):
    add(f'<text x="{x}" y="{y}" class="t-note" text-anchor="{anchor}">{text}</text>')

# ---------------------------------------------------------------- rails
TOP = {"5V": 36, "3V3": 54, "GND": 72, "BATT": 90}
LRAIL = {"5V": 26, "3V3": 40, "GND": 54, "BATT": 68}       # vertical, left side
RRAIL = {"3V3": 1146, "GND": 1160, "5V": 1174}             # vertical, right side
RAIL_BOTTOM = 735

# top buses bridge the two sides; the devkit taps them
for name, y in TOP.items():
    x1 = RRAIL.get(name, LRAIL[name])
    wire([(LRAIL[name], y), (x1, y)], COL[name], 3)
for rails in (LRAIL, RRAIL):
    for name, x in rails.items():
        wire([(x, TOP[name]), (x, RAIL_BOTTOM)], COL[name], 3)
        add(f'<text x="{x+4}" y="{RAIL_BOTTOM-6}" class="t-bus" fill="{COL[name]}" '
            f'transform="rotate(-90 {x+4} {RAIL_BOTTOM-6})">{RAIL_LABEL[name]}</text>')

def to_lrail(px, py, rail, dashed=False):
    wire([(px, py), (LRAIL[rail], py)], COL[rail], dashed=dashed); dot(LRAIL[rail], py, COL[rail])

def to_rrail(px, py, rail, dashed=False):
    wire([(px, py), (RRAIL[rail], py)], COL[rail], dashed=dashed); dot(RRAIL[rail], py, COL[rail])

# ---------------------------------------------------------------- shield (feeds the left rails)
MX, MW = 110, 200                 # left module column
box(MX, 104, MW, 108, "Battery Shield V8", "2×18650 · switch ON · GND = star point")
for name, lab, y in [("5V", "5V ×4", 150), ("3V3", "3V ×4", 166), ("GND", "GND", 182), ("BATT", "B+ tab", 198 - 2)]:
    pin(MX, y, lab, "l"); to_lrail(MX, y, name)
note(MX + MW + 10, 122, "Shield USB-A → devkit USB-C: alternative supply for the devkit only")

# ---------------------------------------------------------------- devkit
DX, DY, DW, DH = 475, 150, 250, 510
box(DX, DY, DW, DH, "ESP32-S3 DevKitC-1", "pins in logical order, not header order")
left = [("GPIO4", 215), ("GPIO5", 245), ("GPIO18", 300), ("GPIO17", 330), ("GPIO7", 360),
        ("GPIO2", 430), ("GPIO1", 470)]
right = [("GPIO8 SDA", 215), ("GPIO9 SCL", 245), ("GPIO13 MISO", 300), ("GPIO12 MOSI", 330),
         ("GPIO11 SCK", 360), ("GPIO10 CS", 390), ("GPIO6 INT1", 470)]
LP, RP = {}, {}
for name, y in left:
    pin(DX, y, name, "l"); LP[name] = (DX, y)
for name, y in right:
    pin(DX + DW, y, name, "r"); RP[name.split()[0]] = (DX + DW, y)
for x, rail, lab in [(660, "5V", "5V pin"), (700, "GND", "GND")]:
    wire([(x, DY), (x, TOP[rail])], COL[rail]); dot(x, TOP[rail], COL[rail])
    add(f'<text x="{x}" y="{DY-6}" class="t-pin" text-anchor="middle">{lab}</text>')
for x, y, label in [(572, 230, "UART1"), (572, 330, "UART2"), (572, 430, "IRQ"), (572, 470, "ADC1"),
                    (640, 230, "I2C0"), (640, 345, "SPI2"), (640, 446, "IRQ")]:
    tag(x, y, label)
add(f'<text x="{DX+10}" y="{DY+DH-30}" class="t-sub">3V3 pin: leave free (devkit LDO output)</text>')
add(f'<text x="{DX+10}" y="{DY+DH-14}" class="t-sub">USB "UART" port → laptop console</text>')

# ---------------------------------------------------------------- left modules
lch = {"n": 0}
def lchan():
    lch["n"] += 1
    return 350 + lch["n"] * 14

def lwire(py, dev, color):
    """From the left module's right edge at y to a devkit left pin."""
    dx, dy = LP[dev]
    xc = lchan()
    wire([(MX + MW, py), (xc, py), (xc, dy), (dx, dy)], color)
    return xc

# GNSS
box(MX, 222, MW, 150, "GNSS Foxeer M10Q-250", "UART1 · NMEA out · 115200 (BE-220: 9600)")
pin(MX, 270, "VCC", "l"); to_lrail(MX, 270, "5V")
pin(MX, 292, "GND", "l"); to_lrail(MX, 292, "GND")
pin(MX + MW, 270, "TX", "r"); lwire(270, "GPIO4", COL["TX"])
pin(MX + MW, 292, "RX", "r"); lwire(292, "GPIO5", COL["RX"])
pin(MX + MW, 318, "SCL", "r"); pin(MX + MW, 340, "SDA", "r")
note(MX + 10, 362, "SCL/SDA: compass (Foxeer only), optional")

# BIGGA GSM
BY = 392
box(MX, BY, MW, 206, "BIGGA GSM (SIM800C)", "UART2 · AT commands · 115200 · VSS = 5 V supply")
g = {"GND": BY + 54, "VCC": BY + 76, "TX": BY + 118, "RX": BY + 140, "BOOT": BY + 162, "NET": BY + 188}
pin(MX, g["GND"], "GND", "l"); to_lrail(MX, g["GND"], "GND")
pin(MX, g["VCC"], "VSS=5V in", "l"); to_lrail(MX, g["VCC"], "5V")
note(MX + 10, g["VCC"] + 18, "100+10+1 µF on VCC are on the board: no external cap")
pin(MX + MW, g["TX"], "TX", "r")
xc = lwire(g["TX"], "GPIO18", COL["TX"])
# R3 to ground hangs off the TX channel, in the gap left of it (nothing else there)
ry = g["TX"] - 64
wire([(xc, ry), (xc - 20, ry)], COL["TX"], 1.6); dot(xc, ry, COL["TX"])
part(xc - 34, ry, 28, 16, "R3")
wire([(xc - 48, ry), (xc - 56, ry)], COL["GND"], 1.6); ground(xc - 56, ry)
add(f'<text x="{xc-34}" y="{ry-12}" class="t-part" text-anchor="middle">6.8k</text>')
note(MX + 10, g["VCC"] + 32, "R3 6.8k on TX → ~3 V at GPIO18")
pin(MX + MW, g["RX"], "RX", "r"); lwire(g["RX"], "GPIO17", COL["RX"])
pin(MX + MW, g["BOOT"], "PK (PWRKEY)", "r"); lwire(g["BOOT"], "GPIO7", COL["CTRL"])
pin(MX + MW, g["NET"], "NET", "r")
wire([(MX + MW, g["NET"]), (MX + MW + 16, g["NET"])], COL["CTRL"])
part(MX + MW + 36, g["NET"], 36, 16, "R4 1k")
wire([(MX + MW + 54, g["NET"]), (MX + MW + 70, g["NET"])], COL["CTRL"])
ax = MX + MW + 70
add(f'<polygon points="{ax},{g["NET"]-7} {ax},{g["NET"]+7} {ax+14},{g["NET"]}" fill="#e03a4e" stroke="var(--fg)" stroke-width="1"/>')
add(f'<line x1="{ax+14}" y1="{g["NET"]-7}" x2="{ax+14}" y2="{g["NET"]+7}" stroke="var(--fg)" stroke-width="1.5"/>')
wire([(ax + 14, g["NET"]), (ax + 30, g["NET"])], COL["GND"]); ground(ax + 30, g["NET"])
note(ax + 44, g["NET"] + 4, "LED1 blinks: 0.8 s searching · 3 s registered")

# Button and battery divider
UY = 610
box(MX, UY, MW, 110, "Button · battery sense", "GPIO interrupt · ADC1 channel 0")
sy = UY + 50
add(f'<text x="{MX+10}" y="{sy-10}" class="t-pin">SW1</text>')
add(f'<line x1="{MX+10}" y1="{sy}" x2="{MX+30}" y2="{sy}" stroke="var(--fg)" stroke-width="1.5"/>')
add(f'<line x1="{MX+30}" y1="{sy}" x2="{MX+52}" y2="{sy-10}" stroke="var(--fg)" stroke-width="1.5"/>')
add(f'<line x1="{MX+54}" y1="{sy}" x2="{MX+74}" y2="{sy}" stroke="var(--fg)" stroke-width="1.5"/>')
wire([(MX, sy), (MX + 10, sy)], COL["GND"]); to_lrail(MX, sy, "GND")
wire([(MX + 74, sy), (MX + MW, sy)], COL["CTRL"]); lwire(sy, "GPIO2", COL["CTRL"])
dy_ = UY + 90
part(MX + 50, dy_, 40, 16, "R1 100k"); part(MX + 130, dy_, 40, 16, "R2 100k")
wire([(MX, dy_), (MX + 30, dy_)], COL["BATT"]); to_lrail(MX, dy_, "BATT")
wire([(MX + 70, dy_), (MX + 110, dy_)], COL["BATT"]); dot(MX + 90, dy_, COL["BATT"])
wire([(MX + 150, dy_), (MX + 172, dy_)], COL["GND"]); ground(MX + 172, dy_)
wire([(MX + 90, dy_), (MX + 90, dy_ - 22), (MX + MW, dy_ - 22)], COL["BATT"]); lwire(dy_ - 22, "GPIO1", COL["BATT"])
note(MX + 10, dy_ + 18, "mid = ½ cell voltage → GPIO1")

# ---------------------------------------------------------------- right modules
RX_, RW = 890, 200
rch = {"n": 0}
def rchan():
    rch["n"] += 1
    return 865 - rch["n"] * 14

def rwire(py, dev, color, xc=None, dashed=False):
    dx, dy = RP[dev]
    xc = xc or rchan()
    wire([(RX_, py), (xc, py), (xc, dy), (dx, dy)], color, dashed=dashed)
    return xc

box(RX_, 150, RW, 130, "SSD1306 OLED", "I2C0 · 100 kHz · address 0x3C")
pin(RX_ + RW, 198, "GND", "r"); to_rrail(RX_ + RW, 198, "GND")
pin(RX_ + RW, 220, "VCC", "r"); to_rrail(RX_ + RW, 220, "3V3")
pin(RX_, 242, "SCL", "l"); x_scl = rwire(242, "GPIO9", COL["SCL"])
pin(RX_, 264, "SDA", "l"); x_sda = rwire(264, "GPIO8", COL["SDA"])

box(RX_, 302, RW, 170, "MicroSD SPI module", "SPI2 · 10 MHz · FAT32 · VCC 3V3 (5V if shifter)")
pin(RX_ + RW, 352, "GND", "r"); to_rrail(RX_ + RW, 352, "GND")
pin(RX_ + RW, 374, "VCC", "r"); to_rrail(RX_ + RW, 374, "3V3")
for lab, dev, y in [("MISO", "GPIO13", 396), ("MOSI", "GPIO12", 418), ("SCK", "GPIO11", 440), ("CS", "GPIO10", 462)]:
    pin(RX_, y, lab, "l"); rwire(y, dev, COL["SPI"])

box(RX_, 500, RW, 160, "BMA400 (later)", "I2C0 · address 0x14 · INT1 interrupt · not fitted", dashed=True)
pin(RX_ + RW, 550, "VCC", "r"); to_rrail(RX_ + RW, 550, "3V3", dashed=True)
pin(RX_ + RW, 572, "GND", "r"); to_rrail(RX_ + RW, 572, "GND", dashed=True)
pin(RX_, 594, "SDA", "l"); wire([(RX_, 594), (x_sda, 594), (x_sda, RP["GPIO8"][1])], COL["SDA"], dashed=True); dot(x_sda, RP["GPIO8"][1], COL["SDA"])
pin(RX_, 616, "SCL", "l"); wire([(RX_, 616), (x_scl, 616), (x_scl, RP["GPIO9"][1])], COL["SCL"], dashed=True); dot(x_scl, RP["GPIO9"][1], COL["SCL"])
pin(RX_, 638, "INT1", "l"); rwire(638, "GPIO6", COL["CTRL"], dashed=True)

# ---------------------------------------------------------------- legend
LX, LY = 475, 735
items = [("5V", "+5V"), ("3V3", "+3V3"), ("GND", "GND"), ("BATT", "B+"), ("TX", "TX"), ("RX", "RX"),
         ("SDA", "SDA"), ("SCL", "SCL"), ("SPI", "SPI"), ("CTRL", "control")]
for i, (k, lab) in enumerate(items):
    x = LX + i * 64
    wire([(x, LY), (x + 20, LY)], COL[k], 3)
    note(x + 24, LY + 4, lab)
note(LX, LY + 22, "dashed = optional or not fitted yet · ⏚ = any GND pin on the shield · dots = junctions")

svg = f'''<svg viewBox="0 0 {W} {H}" xmlns="http://www.w3.org/2000/svg" role="img" aria-label="Wiring diagram: devkit in the centre, modules left and right, power rails down both sides">
<style>
  .t-title {{ font: 600 14px var(--font-display); fill: var(--fg); }}
  .t-sub {{ font: 11px var(--font-body); fill: var(--muted); }}
  .t-pin {{ font: 11.5px var(--font-mono); fill: var(--fg); }}
  .t-part {{ font: 10px var(--font-mono); fill: var(--fg); }}
  .t-note {{ font: 11px var(--font-body); fill: var(--muted); }}
  .t-bus {{ font: 600 11px var(--font-mono); }}
  .t-tag {{ font: 600 10px var(--font-mono); fill: var(--muted); letter-spacing: 0.04em; }}
</style>
{chr(10).join(out)}
</svg>'''
print(svg)
