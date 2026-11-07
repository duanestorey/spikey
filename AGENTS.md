# AGENTS.md — PocketPCR (fork)

Notes for AI coding agents and humans working on this repo. This file holds a project review (Nov 2026), how to build and flash the firmware, known bugs, and the plan for adding **touchdown PCR**.

## What this is

A fork of [GaudiLabs/PocketPCR](https://github.com/GaudiLabs/PocketPCR): a pocket-size, USB-powered PCR thermocycler that holds 5 tubes. It uses an aluminium block with a bottom heater, a fan for cooling, a 128×64 OLED screen and a rotary encoder with a push button.

- `origin` = this fork, `upstream` = GaudiLabs. When reviewed, both were at the same commit (`daf77fc`), so nothing from upstream is missing.
- License: GPL (see `LICENSE`).

## Repository layout

| Path | Contents |
|---|---|
| `code/PocketPCR_V2_quite_dev_i_disp/` | **All firmware.** The main sketch `.ino` (~700 lines) plus bundled copies of the `Rotary` (encoder) and `FlashStorage` (SAMD flash-as-EEPROM) libraries |
| `PCB_Layouts/PocketPCR_PCB/RocketPCR/` | Main board KiCad (old KiCad 5 format) files and Gerbers. "RocketPCR" is just the old project name. |
| `PCB_Layouts/PocketPCR_Lid/` | Lid PCB (KiCad and Gerbers). Mechanical/cosmetic only; there is no lid heater on this device. |
| `PCB_Layouts/PocketPCR_BottomHeater/` | Heater PCB, DXF outlines, NTC lookup sheet (`PCR_Temp_NTCG103.ods`) |
| `PocketPCR_BOM.ods`, `Parts/` | Bill of materials and a heater drawing |

Not in the repo: enclosure/3D files, build docs, the Adafruit display libraries, and any tests or CI.

## Hardware ↔ firmware pin map (checked against `RocketPCR.net`)

The MCU is an **ATSAMD21G18** (TQFP48). The firmware uses **Arduino Zero** pin numbering, and every pin below matches the schematic:

| Function | Net | SAMD21 pin | Arduino pin in code |
|---|---|---|---|
| Block thermistor (NTC 10k, B=3435, 4.7k divider) | TEMP_BLOCK | PB09 | `A2` |
| Bottom heater MOSFET (PWM) | BLOCK | PA06 | `8` |
| Fan MOSFET (PWM) | FAN | PA08 | `4` |
| Encoder A / B | ENC1 / ENC2 | PA20 / PA21 | `6` / `7` |
| Encoder push button | SW1 | PA05 | `A4` |
| OLED MOSI / CLK (software SPI) | DISP_SDA / DISP_SCL | PA22 / PA23 | `20` / `21` |
| OLED DC / CS / RESET | DISP_DC / DISP_CS / DISP_RES | PA11 / PA12 / PA15 | `0` / `22` / `5` |
| USB | D- / D+ | PA24 / PA25 | native USB |
| SWD (bootloader recovery) | SWDIO / SWCLK | PA31 / PA30 | header `J2` |

**Not used on PocketPCR:** `LID` (PA07 / D9, MOSFET Q3) and `TEMP_LID` (PB08 / A1). These lid parts look like leftovers from an earlier design. GaudiLabs' qPocketPCR is a **separate device** (an ESP32 with a camera and a TFT screen, repo `GaudiLabs/qPocketPCR`), not this board. `lidPin` is declared in the code but never driven, and that is expected. **Do not "fix" this.** For safety it is fine to drive D9 LOW explicitly.

## Building and flashing

Yes, this code should build and flash to the device with the stock Arduino SAMD core. Not yet verified on hardware in this fork.

Arduino IDE:
1. Boards Manager → install **Arduino SAMD Boards (32-bits ARM Cortex-M0+)**.
2. Library Manager → install **Adafruit GFX Library** and **Adafruit SSD1306** (2.x; the last upstream commit adapted the code to the newer SSD1306 constructor).
3. Board: **Adafruit Feather M0** (needs the Adafruit SAMD package; the board manager URL is `https://adafruit.github.io/arduino-board-index/package_adafruit_index.json`). GaudiLabs' shipped V1.03 was built this way, and the device reports USB ID 239A:800B. Arduino Zero (Native USB Port) also works, because every pin this firmware uses maps to the same chip pin. Open `code/PocketPCR_V2_quite_dev_i_disp/PocketPCR_V2_quite_dev_i_disp.ino` and upload.

arduino-cli:
```sh
arduino-cli core install arduino:samd adafruit:samd --additional-urls https://adafruit.github.io/arduino-board-index/package_adafruit_index.json
arduino-cli lib install "Adafruit GFX Library" "Adafruit SSD1306"
arduino-cli compile --fqbn adafruit:samd:adafruit_feather_m0 code/PocketPCR_V2_quite_dev_i_disp
arduino-cli upload  --fqbn adafruit:samd:adafruit_feather_m0 -p <port> code/PocketPCR_V2_quite_dev_i_disp
```

Notes:
- Upload over USB needs the Arduino Zero bootloader on the device; production units should already have it. If the port doesn't appear, double-tap reset to enter the bootloader. On a blank or bricked board, burn the bootloader through the SWD header `J2` (J-Link, Atmel-ICE, or a CMSIS-DAP probe with "Burn Bootloader").
- **Back up the device firmware before the first upload.** The Arduino-bundled `bossac` (1.7.0-arduino3, at `~/Library/Arduino15/packages/arduino/tools/bossac/1.7.0-arduino3/bossac`) has no `--offset` option. It always starts at 0x2000, just above the 8 KB bootloader. With the device in bootloader mode (double-tap reset):
  ```sh
  bossac -U true --port=cu.usbmodemXXXX -r pocketpcr_backup.bin       # read app flash; expect 253952 bytes (0x3E000)
  bossac -U true --port=cu.usbmodemXXXX -e -w -v -R pocketpcr_backup.bin  # restore
  ```
- **Uploading new firmware erases the saved cycle settings.** `FlashStorage` lives inside the program's flash, so settings reset to defaults on every upload.
- Hold the button while powering on for left-handed mode (screen rotated 180°).
- No toolchain was available during the review, so the code has not been compiled in this fork yet. Do that first.

## How the firmware works

All of it is in the `.ino`, in a single `loop()` with no RTOS:

- **Temperature:** each loop turns the heater off, reads the ADC, then turns the heater back on, so heater current doesn't disturb the reading. The thermistor value is converted to °C with the B-parameter equation and smoothed with `temperature_mean = (3*old + new)/4`.
- **Control:** a PID-style controller (`PIDp 0.5`, `PIDi 0.0001`, `PIDd 0.15`). Its output goes through `power_heating()` / `power_cooling()`, which use fitted constants (`FA_*`) that model the block's heat loss. Positive output drives the heater; negative output drives the fan. The I term only switches on once the target has been reached.
- **UI state machine** (`caseUX`): `CASE_Main` → `CASE_Settings` / `CASE_EditSettings` → `CASE_Run` → `CASE_Done`.
- **PCR state machine** (`casePCR`): `PCR_set` (load the target) → `PCR_transition` (ramp until within ±0.5 °C) → `PCR_time` (hold for the set time). `PCRstate` steps through 0 Init, 1 Denature, 2 Annealing, 3 Extension, 4 Final. After 3 it loops back to 1 until `PCRcycle > settings.value[10]`.
- **Settings:** `settings.value[12]` (ints), with temps at even indices and times at odd ones: `[0,1]` Init, `[2,3]` Denature, `[4,5]` Anneal, `[6,7]` Extension, `[8,9]` Final, `[10]` cycle count, and `[11]` used as an "already set up" flag (`==0` → write defaults). Times over 90 s are shown and edited in minutes.
- The encoder interrupt (`rotate()`) increments or decrements a global `counter`, and the current screen interprets it.

## Review findings (what to fix)

In priority order. Line numbers refer to the `.ino` at commit `daf77fc` (tag `gaudilabs-v1.02`). Items marked **FIXED (Storey 0.0.1b)** are done on branch `storey-1.0`.

1. **FIXED (Storey 0.0.1b).** **No protection against thermal runaway (high).** If the thermistor is disconnected (ADC ≈ 4095) or shorted (ADC ≈ 0), the temperature calculation (~L201–203) produces a very low or NaN reading, and the controller runs the heater at full power indefinitely. Add: limits on the raw ADC value, a NaN check, a maximum temperature cutoff (e.g. >105 °C), and a "heating but temperature not rising" timeout. On any fault, turn the heater off and the fan on, and show an error screen. The block thermistor is the device's only temperature sensor.
2. **FIXED (Storey 0.0.1b).** **A second run in the same session does only one cycle (high).** `PCRcycle` is set to 1 at power-on and never reset when a run starts (L227). Reset `PCRcycle`, `TEMPi`, `TEMPdif_a[]` and `PIDIntegration` when a run starts.
3. **Partly fixed (Storey 0.0.1b):** the waits now turn the heater off first; they still block the loop. **Waiting for a button release freezes the controller (medium).** `while(!digitalRead(butPin));` is used everywhere, including during a run (L556). While the button is held, the heater stays at its last PWM value with no temperature control. Replace these waits with a non-blocking edge or debounce check.
4. **The I term depends on loop speed (low/medium).** `TEMPi` grows once per loop pass instead of over time; the D term uses 200 ms samples. Changing how long drawing takes changes the control behaviour. Base the PID on a fixed time step (e.g. 100–200 ms) using `millis()`.
5. **Stop is awkward (low).** "Press to STOP" only works when `counter` is odd, so you have to turn the knob one click first.
6. **Minor:**
   - The defaults are unusual (denature 120 s per cycle, no 72 °C final extension).
   - The variable `x` in `draw_run_display` is always 0.
   - ~~"Left Handed Mode" text is always shown on the splash screen.~~ Replaced by a hint (Storey 0.0.1b).
   - ~~Pressing Cancel reads `settings.value[12]`, one past the end of the array.~~ Fixed (Storey 0.0.1b).
   - `SETTING_String` is sized 6 but only 5 entries are filled in.
   - Software SPI to the OLED is slow and makes the loop slower.

There are no tests. If you add logic, keep the pure parts (temperature conversion, touchdown schedule, state transitions) in functions with no hardware calls, so they can be unit-tested on the host later.

## Changes in this fork

Branch `storey-1.0`, firmware version **Storey 0.0.1b**. Versioning: beta releases are `Storey 0.0.Nb` (N increments each release) until launch at `1.0.0`; each release is tagged `storey-0.0.Nb`. Baseline tag: `gaudilabs-v1.02` (= upstream `daf77fc`). Build size (Feather M0, Adafruit SAMD 1.7.17): 59.6 KB flash (22%).

- **Version and splash screen:** a 2 s splash (`SPLASH_MS`) in the small built-in font, centred horizontally and vertically: "PocketPCR" / "by GaudiLabs" / (blank) / "Version" / "Storey 0.0.1b" (`VersionString`). The original splash had no delay, so it was never visible. Holding the button during the splash selects left-handed mode.
- **Run screen redesign:**
  - Title bar "PCR - n of N" (0 during Init, N during Final), or "Press to STOP PCR" after turning the knob.
  - Rows: "State:"; "Temp: set°C / block°C"; a separator; "Cycle Time:" (seconds left in the current step); "Elapsed:" (h:mm:ss since Run was pressed, from `runStart`); "Remaining:" (h:mm:ss, estimated).
  - The estimate (`estimateSecondsLeft()`) walks through the rest of the program in the same order as the state machine. For each remaining step it adds the ramp time (|ΔT| ÷ rate) and the hold time.
  - Ramp rates start at `HEAT_RATE_DEFAULT` / `COOL_RATE_DEFAULT` (1.04 / 1.01 °C/s, measured on a 1-cycle run on 2026-11-07, so only a few ramps). After each real ramp of more than 3 °C, `learnRampRate()` blends the measured rate into them, so the estimate tightens after the first few steps. The learned rates last until power-off.
- **Done screen summary** (`draw_done_display()`):
  - Title bar "PCR Done", or "PCR Stopped" if the run was stopped.
  - "Total Time:" for this run, and "Estimated:" (the estimate made at the start).
  - "Heat:" and "Cool:" for this run: time-weighted °C/s, and the average seconds per ramp (`~12s`), from `runHeat*`/`runCool*`.
  - A separator, then "Cooling: xx.x°C" (auto cool-down running) or "Block: xx.x°C".
- **Ramp statistics in flash** (`RampStats`, a second `FlashStorage` named `stats_store`, separate from the settings so Set/Cancel don't touch it):
  - **Contents:** overall heat/cool rates; per target step (0 Init … 4 Final) the number of ramps measured, mean/min/max °C/s, and the last ramp's ΔT and seconds; runs started/completed; and for the last completed run, the estimate made at the start (`lastEstSecs`) and the actual duration (`lastActualSecs`).
  - **Measuring:** each ramp of at least `RAMP_MIN_DELTA` (3 °C) is measured when it reaches its target (`learnRampRate()`).
  - **Saving:** written to flash **once per run**, when Done is entered (finished or stopped), to limit flash wear. Also printed to USB serial at 115200 (`printStats()`).
  - **Use in the estimate:** `rampSeconds()` uses the step's own mean rate when it has data in the same direction, and the overall heat/cool rate otherwise.
  - **Reset:** a magic number (`STATS_MAGIC`) resets the stats when the layout changes. A **firmware upload also wipes them**, because FlashStorage lives in program flash.
  - **Planned:** a debug page to view these on the device, under the future Settings menu.
- **Run state reset:** starting a run resets `PCRcycle`, `TEMPi`, `TEMPd`, `TEMPdif_a[]` and `PIDIntegration`, so back-to-back runs do the full cycle count.
- **Heater off while waiting for the button:** every blocking wait for a button release goes through `waitRelease()`, which calls `setHeater(0,0)` first.
- **Out-of-range read fixed:** pressing on Set/Cancel (items 11 and 12) no longer reads `settings.value[12]`.
- **Safety cutoff:** `checkSafety()` runs every loop except on the error screen, and `tripError()` switches to the new `CASE_Error` state: heater off, fan on, an error screen with the block temperature, and a button press returns to the main menu (a fault that's still there trips again). Trip conditions:
  - raw ADC ≤ `ADC_MIN` (20) or ≥ `ADC_MAX` (4075), or a NaN temperature: "SENSOR ERR"
  - `temperature_mean > T_MAX` (105 °C): "OVER TEMP"
  - during a run, heater at full power (`heatPower >= 250`) while more than `NOHEAT_BELOW` (5 °C) below target, with less than `NOHEAT_RISE` (2 °C) rise in `NOHEAT_MS` (60 s): "NO HEATING"
  - The running average `temperature_mean` is only updated from valid readings, so a bad reading can't leave it stuck at NaN.

- **Auto cool-down on the Done screen** (`CASE_Done`). When a run finishes or is stopped, the heater stays off and the fan runs at full speed. Every `COOL_SAMPLE_MS` (30 s) the firmware compares the block temperature with the previous sample. Once it has dropped less than `COOL_STABLE_DROP` (0.3 °C), the block counts as near room temperature and the fan stops. `COOL_TIMEOUT_MS` (60 s) is a hard limit: the fan never runs longer than that. The 10-minute original never seemed to stop in practice. Pressing the button stops cooling and returns to the main menu. The screen shows "Cooling xx.x °C", then "Block xx.x °C". This means the **Final step can be used as a real final extension** (e.g. 72 °C / 5 min) instead of a 25 °C hold. The fan can't get below room temperature, so a 25 °C step may never reach its ±0.5 °C window and finish.

## Goal: touchdown PCR

In **touchdown PCR**, the annealing temperature starts several degrees above the primers' Tm and drops by a fixed step each cycle, typically 0.5–1 °C per cycle, until it reaches the target. The remaining cycles then run at that target temperature. This reduces non-specific products in early cycles.

### Agreed design and roadmap

**The current design and roadmap are in [`docs/superpowers/specs/2026-11-07-touchdown-design.md`](docs/superpowers/specs/2026-11-07-touchdown-design.md).** That doc supersedes earlier notes here. Summary:

- **Order of work:** groundwork (compile setup, firmware backup, run-state reset, safety cutoff, out-of-range read fix) → touchdown → Settings item on the main menu → named saved programs (5-character names entered with the knob) → program types (PCR / Hold).
- **Touchdown settings:** a new `td_start` field; `value[4]` becomes the anneal *end* temperature. Touchdown is on when `td_start > value[4]`. Defaults are 63 → 55 °C, with a fixed 1 °C/cycle step (`TD_STEP`).
- **Cycle count:** touchdown cycles (`td_start - value[4]`) are added **before** the normal cycle count.
- **Editing:** done on the Annealing bar as a range. Pressing once edits the top (start) and pressing again edits the bottom line (end). Menu numbering 0–12 is unchanged.
- **Saved settings:** a `magic` field replaces the `value[11]` "already set up" flag.

## Conventions for agents

- Keep the sketch building with the stock Arduino SAMD core and Library Manager libraries. Don't add a build system without discussing it first.
- Keep the code style (Arduino globals, `#define` state names). Keep changes small and explain any change to the controller.
- Anything that can drive the heater must fail safe: heater off by default, and on any doubt about the temperature reading.
- Don't modify Gerber or production files under `PCB_Layouts/**/Gerber/` unless asked.
