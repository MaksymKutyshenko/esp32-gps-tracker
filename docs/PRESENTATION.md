# Defense: slides and demo plan

Built on the structure Module 7.3 asks for (hook → problem → solution →
implementation → demo → next) and the demo structure from Module 7.4
(happy path, one edge case, one architectural decision, measured data).
Use the academy's slide template; 10–12 slides, one idea per slide,
diagrams over text, concrete numbers.

## Slides

1. **Hook** — "The card said the bike was parked all afternoon. The server
   said it was in another district." One sentence of the problem, no
   solution yet.
2. **Problem** — a tracker must keep a truthful record without coverage,
   report live when it can, and not die quietly. Three failure modes: lost
   records, false positions, silent hangs.
3. **Hardware** — photo of the build plus the GPIO map (from
   `docs/HARDWARE.md`). ESP32-S3, GNSS, OLED, SD, SIM800C, 2×18650.
4. **Architecture** — the task/queue diagram (README). Four layers, six
   tasks, queues only, one mutex per bus. Say why queues and not shared
   state.
5. **State machine** — the FSM diagram. Point at the table in
   `TrackerFsm.hpp`: "the diagram is generated from this table, the tests
   walk it".
6. **Input validation** — NMEA checksum, field checks, "a rejected sentence
   leaves the previous fix untouched". Show the parser test list.
7. **Feedback loop** — LogPolicy: measured motion → interval; the
   speed-hysteresis plot or numbers (150 cm/s start, 50 cm/s stop, 30 s
   delay). UplinkScheduler back-off curve.
8. **Reliability** — the table from `docs/ARCHITECTURE.md` §4: watchdog,
   remount, modem power cycle, BusLock RAII. Reset-reason log line.
9. **Demo** — live (see below).
10. **Measured** — currents (idle / tracking / uploading), stack headroom,
    CPU load, soak time, upload success rate from `diag`.
11. **Known issues and next** — BMA400 + deep sleep, batching, GPX, UBX.
12. **Questions** — keep the "what / why / what if" template in mind.

## Live demo (≈5 minutes)

1. **Happy path** — power on, LED white → amber → green, fix on the OLED,
   `diag` shows `nmea rx` growing and `log ok` ticking; show the CSV line
   format on the console; Traccar web page shows the point.
2. **Edge case (pick one, rehearse it)**
   - Pull the MicroSD mid-run: `Record not written`, `SD --` on screen,
     then reinsert: `Recovering storage: remounting card`, `SD ok`, records
     continue, `lost` count tells the truth.
   - Cover the antenna: state `SEARCH` → after 5 s silence `NO GPS` (red);
     uncover: back to `TRACK`.
   - Unplug the GSM antenna: uploads fail, back-off grows in the log, queue
     holds the records; replug: backlog drains, `sent` catches up.
3. **Architectural decision** — "Records are popped from the uplink queue
   only after HTTP 200. Alternative: pop before sending and keep a retry
   buffer; rejected because it duplicates state and loses the record on a
   reset in between."
4. **Numbers** — the multimeter readings and the minute diagnostics line.

## Questions to be ready for

- Why a mutex on I2C when only the display uses it today? (The BMA400 is
  the second client; the lock is already RAII so adding it cannot leak.)
- Why `xQueueOverwrite` for the display and a deep queue for the logger?
  (The screen wants the newest value; the card wants every value.)
- What happens if the gps task is starved? (Highest priority of the five;
  the receiver is set to 1 Hz and the UART ring buffer holds seconds of
  it; `rej`/`silences` counters
  would show it.)
- Why no `portMAX_DELAY` anywhere? (A wait that never ends is a hang the
  watchdog turns into a reset; bounded waits turn it into a counted error.)
- Why is the FSM table-driven and not one class per state? (Five states,
  fifteen transitions: a table is read in one screen and tested by walking
  it; class-per-state would add six files for no behaviour.)
- How is `esp_reset_reason()` used? (Logged at boot; `TASK WATCHDOG` or
  `BROWNOUT` in that line is the first thing to look at after a field
  failure.)

## Before the defense

- [ ] Fill the checklist copy (≥ 80 %) with evidence links (file:line).
- [ ] README results section with measured numbers.
- [x] KiCad schematic in `docs/kicad/` (regenerate with `tools/gen_schematic.py` after pin changes).
- [ ] GitHub release v1.0.0 with a changelog.
- [ ] Rehearse the edge case twice.
