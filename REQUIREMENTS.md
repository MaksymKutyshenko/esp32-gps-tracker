# GPS Tracker — Final Project Requirements

Compiled on 2026-10-09 from the Beetroot Academy "Embedded Development" LMS:
lesson 59 (topics and requirements), lessons 70–74 (Module 7.1–7.5), the
official grading spreadsheet, and the two project guides used as a template
for how the academy phrases a project.

Sources (course account required):

- Lesson 59 — topics and requirements: `https://lms.beetroot.academy/studying/cmjjvsqs2pfs40813pfny0kyj/theory/cmnjf6uqox0bh08132wn5qj1u`
- Lesson 70 — Module 7.1 Architecture: `https://lms.beetroot.academy/studying/cmjjvsqs2pfs40813pfny0kyj/theory/cmnz12dbti8du0813d07g0qzh`
- Lesson 71 — Module 7.2 Q&A 1 (shared resources, mutex, deadlock): `.../theory/cmnz12dbyi8dy0813b4b8kp59`
- Lesson 72 — Module 7.3 Presentation and documentation: `.../theory/cmnz12dc4i8e20813a95wnw46`
- Lesson 73 — Module 7.4 Q&A 2 (filters, low power, edge cases, defense): `.../theory/cmnz12dcbi8e60813zerr0mjy`
- Lesson 74 — Module 7.5 Final preparation (flex session, no new content): `.../theory/cmnz12dchi8ea0813be3xyw7n`
- Grading checklist (view): `https://docs.google.com/spreadsheets/d/1Iu5cNJzkI2xzN8xCUNbdvLri1tl0FekqwSjkf1ma6-c/edit`
- Grading checklist (make your own copy): `https://docs.google.com/spreadsheets/d/1Iu5cNJzkI2xzN8xCUNbdvLri1tl0FekqwSjkf1ma6-c/copy`
- Presentation template: `https://docs.google.com/presentation/d/18r_PORkFUQboFu8D0Yn22OFulu35ksLFD7X8-FfxxPE/edit`

> **Topic note.** The academy lists four official topics (Digital Safe,
> Environment Logger, Audio Player, BLE Keyboard). A GPS tracker is a custom
> topic in the "needs extra components" category. Confirm it with the teacher
> or group manager before investing in hardware; the generic checklist below
> is what gets graded either way.

---

## 1. What the defense looks like

- Live demonstration of the prototype, pass/fail grading, in front of the
  group, teacher, possibly other teachers and external experts.
- **Pass threshold: at least 80 % of the technical checklist** (section 3).
  34 of 42 items. The academy's example sheet hits exactly 80.95 % by skipping
  all of "Performance" and the PCB layout items, so that is the floor with
  zero margin, not a target.
- Missing architecture diagram = **−20 % immediately** (Module 7.1).

### Links to hand in before the defense

1. Final presentation (Google Slides from the template, 10–12 slides).
2. README (Markdown, in the repository).
3. Public GitHub repository with code, schematics and README.
4. Your own copy of the self-check spreadsheet, filled in, ≥ 80 %.

Also expected by Module 7.3: a GitHub **Release v1.0.0** with a changelog
(SemVer), repository description and topics set.

### Demo structure (Module 7.4)

1. **Happy path** — full normal operation without stopping.
2. **Edge case** — deliberately provoke one abnormal situation and show the
   handling (for the tracker: pull the GPS antenna / block the sky, remove the
   SD card mid-log, corrupt an NMEA sentence).
3. **One architectural decision** explained with the formula
   "I chose X because Y. The alternative Z did not fit because W."
4. **Measured data** — Saleae capture, serial log, or multimeter readings.
   Claims like "I optimised power" are not accepted without numbers.

Do not fix code during the demo, do not say "it just started working", do not
hide bugs. A "Known issues" section in the README is expected, not a weakness.

---

## 2. Grading criteria (the four axes)

Every final project is checked on the same four axes (Module 7.1):

| Axis | What is checked |
| --- | --- |
| Architecture | Layers (Periph / Logic / Transport-Output / Reliability), one responsibility per task, FSM drawn before coding, block diagram present |
| Peripherals | GPIO map with pin, function and voltage; interfaces actually used and explained |
| RTOS | Tasks, queues, mutexes, no race conditions, no `delay()` patterns in tasks |
| Algorithm / control | Feedback, filtering or regulation, configurable parameters, boundary handling |

For the official topics the weights are Architecture 20 %, Functionality
30–40 %, Documentation 20 %, and a topic-specific reliability/optimisation
axis 20–30 %. For a custom topic assume the same split and treat
**reliability (WDT + peripheral error recovery)** as the fourth axis, since the
Environment Logger guide (the closest official topic) weighs it at 30 %.

---

## 3. Technical checklist (official spreadsheet, 42 items)

Numbering matches the spreadsheet so the copy can be filled from this table.
"Plan" is how the GPS tracker satisfies the item; **TBD** means it depends on
the component list.

### 1. Architecture

| # | Requirement | Plan |
| --- | --- | --- |
| 1.1 | Superloop or RTOS used | FreeRTOS tasks (ESP-IDF) |
| 1.2 | No blocking `delay()` in the main logic | `vTaskDelayUntil` / blocking reads only inside the owning task |
| 1.3 | State machine implemented | Tracker FSM: `NO_FIX → ACQUIRING → TRACKING → LOGGING`, plus `ERROR` states for storage / GPS silence |
| 1.4 | Architecture block diagram exists | Draw layers + tasks + queues before coding; put in README |
| 1.5 | Logic split into modules (files / drivers) | One class per header/source pair, as in the homework repo |

### 2. Peripherals and interfaces

| # | Requirement | Plan |
| --- | --- | --- |
| 2.1 | At least one analog subsystem (ADC / DAC / PWM / I2S / PDM) | ADC battery-voltage monitor through a divider; PWM buzzer for fix / alerts (**TBD** by components) |
| 2.2 | At least one digital interface (UART / I2C / SPI) | UART for the GPS module (NMEA); I2C for the OLED; SPI for the SD card |
| 2.3 | Interrupts used | Button via GPIO ISR → `xQueueSendFromISR`; GPS PPS pin interrupt if the module exposes it; UART driver RX interrupts |
| 2.4 | Asynchronous data transfer (buffer / queue) | Queues between GPS, display and logger tasks; UART ring buffer |
| 2.5 | Timers or hardware events configured | `esp_timer` / GPTimer for the logging interval; PPS as a hardware event |

### 3. RTOS (if used)

| # | Requirement | Plan |
| --- | --- | --- |
| 3.1 | At least 2 tasks | `gps_rx`, `display`, `logger`, `ui` |
| 3.2 | Queue used | Single-slot "latest fix" queue for the display; deep queue for the logger (every fix must be logged) |
| 3.3 | Mutex or other synchronisation | One mutex per shared bus (I2C, SD/SPI) and one for the UART log |
| 3.4 | No race conditions (superloop users must argue why RTOS is unnecessary) | All shared state goes through queues; ISR-shared variables `volatile`; only `*FromISR` APIs in ISRs |

### 4. Control and algorithms

| # | Requirement | Plan |
| --- | --- | --- |
| 4.1 | Feedback loop implemented | Adaptive logging: measured speed / distance since last point decides the next log interval (closed loop on movement). **Weakest item — decide early** |
| 4.2 | PID or another regulation algorithm | EMA / moving-average filter on speed and altitude; optional: PID on bearing error driving a servo "home pointer" using GPS course over ground |
| 4.3 | Parameters configurable | Log interval, units, speed threshold stored in NVS, editable via button menu or console |
| 4.4 | Boundary conditions handled (overflow, saturation) | NMEA line length cap, lat/lon range checks, odometer in `uint32_t` metres with saturation, date rollover, SD full |

### 5. Reliability

| # | Requirement | Plan |
| --- | --- | --- |
| 5.1 | WDT used | `esp_task_wdt` on every task; log `esp_reset_reason()` at boot |
| 5.2 | Peripheral error handling | GPS silent > N s → `NO_FIX` + warning; SD write error → retry ×3 then remount; I2C NACK → bounded retry then bus reset |
| 5.3 | Invalid data checked | NMEA checksum, fix status (`A`/`V`), satellite count / HDOP threshold, timestamp sanity |
| 5.4 | System does not hang on bad input | Garbage on the UART, missing SD card and unplugged OLED must all be survivable; demonstrate at least one |

### 6. Performance and optimisation (all optional in the example sheet)

| # | Requirement | Plan |
| --- | --- | --- |
| 6.1 | Execution time of critical sections measured | `esp_timer_get_time()` around NMEA parse and display flush; numbers in README |
| 6.2 | DMA used where appropriate | SD over SPI uses DMA by default; UART DMA (UHCI) from module 4.6 is available if the log stream justifies it |
| 6.3 | No redundant data copies | Parse in place from the UART buffer; zero-copy line buffers for logging |
| 6.4 | CPU not overloaded (> 70 %) | `vTaskGetRunTimeStats()` snapshot in README |

### 7. PCB (manufacturing optional, **project files mandatory**)

| # | Requirement | Plan |
| --- | --- | --- |
| 7.1 | Schematic created | KiCad schematic of the whole tracker (required for 9.4 too) |
| 7.2 | 2-layer PCB routed | Optional; do it if time allows |
| 7.3 | Power filtering designed in | Decoupling on GPS and OLED supplies, bulk cap on the SD card |
| 7.4 | Power and logic sections separated | Separate ground return for the buzzer / any motor |
| 7.5 | High-speed signal routing considered | Keep the GPS antenna / RF line and SPI short; note it in the schematic |
| 7.6 | Test points added | UART TX/RX of the GPS, 3V3, GND |
| 7.7 | Board manufactured and tested | Optional |

### 8. Documentation

| # | Requirement | Plan |
| --- | --- | --- |
| 8.1 | README with task description | See README structure below |
| 8.2 | Block diagram | Layers + tasks + queues |
| 8.3 | Architecture description | FSM, tasks, queues, mutexes, why |
| 8.4 | Run instructions | `pio run -t upload -t monitor`, wiring table |
| 8.5 | Interfaces explained | UART/NMEA, I2C, SPI/SD, ADC |

### 9. Demonstration

| # | Requirement | Plan |
| --- | --- | --- |
| 9.1 | Runs stably ≥ 5 minutes | Soak test with WDT enabled; keep a log |
| 9.2 | Edge modes demonstrated | No fix, SD removed, bad NMEA |
| 9.3 | Architecture explained | Prepare 2–3 "hard places" in the code with answers |
| 9.4 | PCB decisions explained | Schematic walk-through |

---

## 4. Architecture rules from Module 7.1

- Draw first, code second: GPIO map, layer diagram, FSM / flow, then mark
  where arrows between tasks need a **Queue** and where two paths to one
  resource need a **Mutex**.
- Four layers every project must show: **Periph** (hardware init),
  **Logic** (FSM, parser, filter), **Transport/Output** (display, log, UART),
  **Reliability** (WDT, error recovery, mutexes).
- One responsibility per task. A task that reads a sensor must not also format
  output.
- Buttons generate **events**, they are not polled in a loop with a delay.
  (The homework repo polls buttons every 20 ms; for this project switch the
  button to a GPIO interrupt feeding a queue so item 2.3 is clearly met.)

Self-check the lesson expects before starting:

- [ ] Can name all four grading axes from memory.
- [ ] Component list and GPIO map written: pin, function, voltage.
- [ ] Hand-drawn block diagram with the four layers.
- [ ] Task list with what each does and where queues sit.
- [ ] Can explain `xQueueSend` vs `xQueueReceive` and when `xSemaphoreTake` replaces a queue.

## 5. Shared-resource rules from Module 7.2

- A resource touched by ≥ 2 tasks, or with mutable state, needs protection.
  Typical: UART (interleaved bytes), I2C bus (NACK / hang), SD card (FAT
  corruption), DMA buffers, ISR → task variables.
- **One mutex per bus**, not per device.
- Always take mutexes in the same order in every task.
- Never `portMAX_DELAY`; use `pdMS_TO_TICKS(100..200)` and log a timeout.
- Always `xSemaphoreGive`, even on the error path.
- In ISRs: only `xQueueSendFromISR` / `portYIELD_FROM_ISR`; never take a mutex.
- Prefer `xSemaphoreCreateMutex()` (priority inheritance) over a binary
  semaphore for resources.
- Diagnostics to have ready: `esp_reset_reason()`, `vTaskList()`,
  `uxTaskGetStackHighWaterMark()` (< 128 bytes free = enlarge), mutex timeout
  logs, logic analyser on I2C (SCL stuck low, endless NACK, missing STOP).

## 6. Filters, power and edge cases from Module 7.4

- **EMA** is the expected filter: `f = a*x + (1-a)*f`; choose `a` from the
  wanted time constant and be able to say why (e.g. 0.2 for display values).
- **Low power** claims need a multimeter measurement before/after and a
  battery-life estimate (`t = capacity / I_avg`). Deep sleep wipes RAM; keep
  state in `RTC_DATA_ATTR` if used. Light sleep keeps UART/I2C alive.
- **WDT** is mandatory: `esp_task_wdt_init`, `esp_task_wdt_add`,
  `esp_task_wdt_reset` in each task loop, reset reason logged at boot.
- **Edge-case categories** to cover: hardware (component missing / unplugged
  during operation / battery low during a write), software (input longer than
  buffer, integer overflow, division by zero in `dt`), timing (two interrupts
  at once, WDT reset mid-write to flash).
- **Defensive coding**: check every return value, bound every retry loop
  (×3 then recover), timeout on every wait.
- Answer template for questions: **What / Why / What if**. If unsure: "I did
  not consider that alternative, but I see why it matters."

## 7. Presentation and README rules from Module 7.3

Presentation (use the template, 10–12 slides, one idea per slide, diagrams
over text, concrete numbers):

1. Hook — one or two sentences of the problem without the solution.
2. Problem — context, why it matters, minimum theory.
3. Solution / architecture — block diagrams; theory only where needed.
4. Implementation — key decisions, not all the code.
5. Demo / results — video, graphs, measured numbers.
6. What next — improvements; be open about limits.

README minimum structure (Module 7.4) merged with the 7.3 template:

```
# Project name
> One sentence: what and why
## About
## Quick start        (clone, pio run -t upload, wiring table)
## Wiring / schematic
## Architecture       (layers, FSM, tasks, queues, mutexes)
## Interfaces         (UART/NMEA, I2C, SPI, ADC)
## Results            (screenshots, measurements, timing, CPU load)
## Known issues and limitations
## What I would improve next
## Documentation      (schematic, datasheets, report)
## Author / Credits   (libraries with licences)
```

Optional technical report: abstract (≤ A4), problem statement, existing
solutions, system description, test results, conclusions, credits.

AI-generated documentation is allowed only as a second pass over your own
draft, with real function names and values, and must be reviewed.

---

## 8. Decisions for the GPS tracker (component list of 2026-10-09)

Hardware details, power tree and the GPIO map are in
[hardware/README.md](hardware/README.md).

| Checklist item | How the chosen hardware covers it |
| --- | --- |
| 2.1 analog subsystem | ADC1 on GPIO1 reads the cell voltage through a divider |
| 2.2 digital interfaces | UART1 GNSS (NMEA), UART2 SIM800C (AT), I2C OLED + BMA400, SPI MicroSD |
| 2.3 interrupts | Button GPIO ISR, BMA400 INT1 motion ISR, UART driver RX interrupts |
| 2.4 async transfer | Queues: latest fix (single slot) to display, every fix to logger and uplink; UART ring buffers |
| 2.5 timers / hardware events | `esp_timer` for log and upload intervals; deep-sleep timer and EXT1 wake on INT1 / button |
| 3.x RTOS | Tasks `gps_rx`, `display`, `logger`, `uplink`, `ui`/`motion`; one mutex per I2C bus and per SD card |
| 4.1 feedback | Motion-adaptive duty cycle: BMA400 activity and measured speed set the log interval, upload interval and sleep decision |
| 4.2 regulation algorithm | EMA on speed/altitude; exponential back-off on GPRS failures driven by signal quality (CSQ) |
| 4.3 configurable parameters | Intervals, server URL, APN, units in NVS, editable from the button menu and console |
| 5.x reliability | NMEA checksum + fix validity, GNSS silence timeout, AT command timeouts with modem power-cycle, SD remount, I2C bus reset, task WDT on every task, reset reason logged |
| 6.2 DMA | SD card over SPI uses DMA; UART DMA optional |
| 9.2 edge cases to demo | Block the sky (no fix), pull the SD card mid-log, unplug the GSM antenna (upload backlog then catch-up) |

### Status (2026-10-13, release v1.0.0)

| Items | Status | Evidence |
| --- | --- | --- |
| 1.1, 1.2, 1.3, 1.5 | done | six FreeRTOS tasks in `firmware/src/App.cpp`; no `delay()` outside bounded waits; `logic/TrackerFsm.hpp` table-driven FSM; one class per file |
| 1.4, 8.2 | done | Mermaid block diagram and FSM diagram in README |
| 2.1 | done | `periph/BatteryMonitor` (ADC1, calibrated) |
| 2.2 | done | UART1 GNSS, UART2 modem, I2C OLED + BMA400, SPI SD |
| 2.3 | done | `periph/Button` and `periph/Bma400` GPIO ISRs; UART driver interrupts |
| 2.4 | done | queues between every pair of tasks; UART ring buffers; SD DMA |
| 2.5 | done | `esp_timer` deadlines, RMT for the LED, task watchdog timer |
| 3.1–3.4 | done | six tasks, seven queues, three `BusLock` mutexes, values-only exchange (`docs/ARCHITECTURE.md` §3) |
| 4.1, 4.2 | done | `LogPolicy` (measured motion → interval), `AccelMotionDetector` / `SpeedMotionDetector` hysteresis, `UplinkScheduler` back-off, `EmaFilter` |
| 4.3 | done | `Settings` in NVS, console `set`/`save`, design values in `config.hpp` |
| 4.4 | done | NMEA range checks, quality gate (satellites, HDOP), queue overflow policy, `Settings::validate` clamps, fixed-point guards, URL-length checks |
| 5.1 | done | `WatchdogSubscription` in every task; reset reason logged and reported upstream |
| 5.2 | done, shown on the bench | GNSS silence timeout, card remount (seen after a rail dip), display re-init (seen), modem bearer re-open and power cycle (seen) |
| 5.3 | done | checksum, field, status, satellite/HDOP and accuracy-mask checks; 15 parser tests |
| 5.4 | done, shown on the bench | charger pulled: card, display, modem all recovered without a reboot; `at AT+CPOWD=1` ×3 |
| 6.1 | partly | stack high-water marks per task in the minute line; no per-section timing yet |
| 6.2, 6.3 | done | SD over SPI with DMA; NMEA parsed in place |
| 6.4 | tool in place | `diag` prints `vTaskGetRunTimeStats`; the number goes into README results after a run |
| 7.1 | done | `hardware/kicad/gps_tracker.kicad_sch`, ERC 0/0; 7.2–7.7 optional (layout, manufacturing) |
| 8.1, 8.3, 8.4, 8.5 | done | README sections; `docs/ARCHITECTURE.md`, `hardware/README.md` |
| 9.1 | done on the bench | hours of uptime through modem bursts, charger changes and sleep cycles |
| 9.2 | done on the bench | SD pulled, display glitched, modem powered down, no-fix, deep sleep and wake |
| 9.3, 9.4 | at the defence | `docs/PRESENTATION.md`, schematic walk-through |

33 items done or demonstrated, 35 with the two defence items: above the
34 needed. 6.1 (per-section timing) and 7.2–7.7 (PCB) are the ones left
out on purpose.

Still open before the defence:

- [ ] Current measurements: tracking, uploading, asleep (multimeter in series with a cell).
- [ ] Outdoor track for the README results and the demo.
- [ ] CPU-load line from `diag` into README results.
- [ ] Slides from `docs/PRESENTATION.md` in the academy template.
- [x] Web app and firmware in one repository (`web/`, `firmware/`), release v1.0.0 on GitHub.
- [ ] Cleanup pass over the repository files.
- [ ] Rehearse the demo and the edge case twice.
