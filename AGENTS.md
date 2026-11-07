# AGENTS.md — Spikey (PocketPCR firmware fork)

Working notes for AI coding agents and humans. This file describes the project **as it is now**: hardware, firmware structure, how to build, back up and flash, and the conventions to follow. For the user-facing description and changelog, see `README.md`.

## What this is

**Spikey** is a firmware fork of [GaudiLabs/PocketPCR](https://github.com/GaudiLabs/PocketPCR), a pocket-size, USB-powered PCR thermocycler for 5 tubes. The device has an aluminium block on a heater PCB, a fan, a 128×64 OLED and a rotary encoder with a push button.

- **Repo:** [duanestorey/spikey](https://github.com/duanestorey/spikey) (`origin`). `upstream` is GaudiLabs.
- **Branch:** work happens on **`1.0.0`**. `master` is still GaudiLabs' original (`daf77fc`, tag `gaudilabs-v1.02`).
- **Versions:** betas are `Spikey 0.0.Nb` (`VersionString`), with N going up on each flashed release, until launch as `1.0.0`. Tags are `spikey-0.0.Nb` from 0.0.6b on. Earlier tags keep their old names: `storey-0.0.1b`, then `spikehole-0.0.2b` and `spikehole-0.0.5b`. The firmware was called Storey, then Spikehole, and is Spikey from 0.0.6b. The GitHub repo was renamed to `spikey` too (GitHub redirects the old Spikehole URL). Add each release to the README changelog.
- **License:** GPL-3.0 (`LICENSE`).
- **Device facts:**
  - The user's unit shipped with GaudiLabs **V1.03 2022**, which was never published (the repo only has V1.02).
  - It was built for the **Adafruit Feather M0** board setting (USB 239A:800B).
  - The bootloader is SAM-BA "v2.0 [Arduino:XYZ]" (239A:000B), not UF2.
  - Its dump is kept **local only**: all `*.bin` files are git-ignored (so are any dumps made in the repo root). Never commit or push it; there's no source for it.

## Repository layout

| Path | Contents |
|---|---|
| `code/PocketPCR_V2_quite_dev_i_disp/` | **All firmware.** One sketch `.ino`, plus bundled `Rotary` and `FlashStorage` libraries |
| `tools/decode_stats.py` | Decodes the ramp statistics from a flash dump |
| `docs/ramp-measurements.md` | Log of measured heat/cool rates per run (empty vs loaded, fan tests). **Add a row after each test run.** |
| `docs/superpowers/specs/2026-11-07-touchdown-design.md` | Design doc and roadmap |
| `firmware-backup/` | README (checksum and restore command for the V1.03 dump) and saved stats files. The `.bin` itself is local only. |
| `Parts/` | Block drawing (GaudiLabs PDF) and a 3D model of the block (FreeCAD/STEP/3MF by Andre Maia Chagas, GPL-3.0). Credits and checksums are in `Parts/info.md`. |
| `PCB_Layouts/PocketPCR_PCB/RocketPCR/` | Main board KiCad 5 files and Gerbers ("RocketPCR" is the old project name) |
| `PCB_Layouts/PocketPCR_BottomHeater/` | Heater PCB: an aluminium-core board with a Ø29 outline, a solid Ø19.5 centre under the block, and 20 radial cooling fins. DXFs, NTC sheet. |
| `PCB_Layouts/PocketPCR_Lid/` | Lid PCB (5 × Ø6 tube holes at r = 7 mm). There is no lid heater. |
| `PocketPCR_BOM.ods` | Bill of materials. The fan is a generic 5 V 3010 (30×30×10 mm, 2-pin) on `J1`. |

There are no tests and no CI. Hardware testing is done by the owner.

## Hardware ↔ firmware pin map (checked against `RocketPCR.net`)

The MCU is an **ATSAMD21G18A** (TQFP48, 256 KB flash / 32 KB RAM). Arduino Zero / Feather M0 pin numbering:

| Function | Net | SAMD21 | Arduino pin |
|---|---|---|---|
| Block thermistor (NTC 10k, B=3435, 4.7k divider) | TEMP_BLOCK | PB09 | `A2` |
| Heater MOSFET (PWM) | BLOCK | PA06 | `8` |
| Fan MOSFET (PWM) | FAN | PA08 | `4` |
| Encoder A / B | ENC1 / ENC2 | PA20 / PA21 | `6` / `7` |
| Encoder button | SW1 | PA05 | `A4` |
| OLED MOSI / CLK (software SPI) | DISP_SDA / DISP_SCL | PA22 / PA23 | `20` / `21` |
| OLED DC / CS / RESET | — | PA11 / PA12 / PA15 | `0` / `22` / `5` |
| USB | D- / D+ | PA24 / PA25 | native |
| SWD (bootloader recovery) | SWDIO / SWCLK | PA31 / PA30 | header `J2` |

**Unused on purpose:** `LID` (D9, Q3) and `TEMP_LID` (A1). They're leftovers from an earlier design. GaudiLabs' qPocketPCR is a different, ESP32-based device. Don't "fix" this.

## Build, back up, flash

See **README → "Backing up and flashing"** for the step-by-step guide. Agent specifics:

- **Build:**
  ```sh
  arduino-cli compile --fqbn adafruit:samd:adafruit_feather_m0 code/PocketPCR_V2_quite_dev_i_disp
  ```
  Current size: about 66 KB (25%).
- **Flash routine** (ask the owner before each flash; the device may be mid-run):
  1. Check the V1.03 backup checksum: `54c55b6f…` (full value in `firmware-backup/README.md`).
  2. Enter the bootloader with a 1200-baud touch on the port.
  3. `bossac -U true --port=… -r dump.bin`, then run `python3 tools/decode_stats.py dump.bin`. If there are stats, save them to `firmware-backup/stats-<date>-<version>.txt` and add a row to `docs/ramp-measurements.md`. **Uploading wipes the stats and the saved programs.**
  4. `arduino-cli compile --upload -p /dev/cu.usbmodemXXX …`. Check that it says "Verify successful" and that the device comes back as 239A:800B.
- **Stuck in the bootloader:** after a 1200-baud touch, `bossac -R` returns to the bootloader, not the app. Power-cycle the device to leave it.
- **Default programs:** after an upload, the defaults below are written to flash. So a flash doesn't force the owner to re-enter a normal program.

## How the firmware works

A single `loop()`, no RTOS. Each pass reads the temperature, runs the safety checks, then handles the current screen.

- **Temperature:** heater off → ADC read → heater back on. B-parameter conversion, smoothed by `temperature_mean = (3*old + new)/4`, updated only from valid readings.
- **Control:** PID (`PIDp 0.5`, `PIDi 0.0001`, `PIDd 0.15`). The output goes through `power_heating()` / `power_cooling()`, fitted block heat-loss models (`FA_*`). Positive output drives the heater, negative drives the fan; they're never on together. The I term only starts once the target has been reached.
- **Safety:** `checkSafety()` runs every loop (except on the error screen). `tripError()` switches to `CASE_Error`: heater off, fan on, an error screen, and a button press returns to Main.
  - "SENSOR ERR": ADC ≤ 20 or ≥ 4075, or NaN
  - "OVER TEMP": above `T_MAX` (105 °C)
  - "NO HEATING": full power for 60 s while more than 5 °C below target, with less than 2 °C rise
  - All blocking button waits go through `waitRelease()`, which turns the heater off first.
- **Screens** (`caseUX`): `CASE_Main` (Run PCR / Run sqPCR / Run RT / Config >) → `CASE_Config` (PCR / sqPCR / RT Program / Debug / < Back; menus with more than 4 items use a 10 px pitch) → `CASE_Settings` / `CASE_EditSettings` (editor, `editType`) or `CASE_Debug` (3 pages); `CASE_Run` ⇄ `CASE_Paused` (sqPCR) → `CASE_Done`; `CASE_Error`. Menu item ↔ type mapping: `menuType()` / `typeMenuIndex()`. Menus use `draw_menu()` / `draw_title()`, a small font with a plain title and a separator.
- **Program types** ("sqPCR" = semi-quantitative PCR, the owner's name for the paused qPCR-style program; keep it) (`runType`: `RUN_PCR` 0 / `RUN_RT` 1 / `RUN_SQPCR` 2). `pcrLike()` covers PCR and sqPCR; `prog(type)` returns `settings` or `sqsettings`. The run code only uses the helpers at the end of the sketch: `stepTemp`/`stepTempAt`, `stepTime`, `totalCycles`, `nextStep`, `stepName`, `statsIndex`, `startRun`.
  - **PCR:** `settings` (`EEprom` in `my_flash_store`). `value[0..9]` are temp/time pairs for Init, Denature, Annealing, Extension and Final; `value[10]` is the **total** cycle count; `value[11]` is the "already set up" flag; `tdStart` is the touchdown start. The cycle loops over steps 1–3.
  - **sqPCR:** `sqsettings` (`EEprom` in `sqpcr_store`): the PCR layout plus `pauseFirst`, `pauseSecond` and `pauseEvery`, all **1–9** (`PAUSE_MAX_N`). Editor items: 10 cycles, 11 first pause (baseline), 12 second pause (> first), 13 every, 14 Set, 15 Cancel. `clampPauses()` keeps first ≤ cycles and first < second ≤ 9/cycles. The cycles box draws `40x P1/6/3` with each segment highlighted separately. Defaults: the PCR steps, 40 cycles, `P1/6/3` (pauses after cycles 1, 6, 9, 12 …).
  - **RT:** `rtprog` (`RTprog` in `rt_store`, `RT_MAGIC`). Incubation, Transcribe and Deactivate, one pass each.
- **Run state machine** (`casePCR`): `PCR_set` → `PCR_transition` (ramp to within ±0.5 °C) → `PCR_time` (hold) → `nextStep()`; −1 means Done.
- **Touchdown** (PCR Annealing only):
  - **On/off:** on when `tdStart > value[4]` and the end isn't Room (`tdOn()`).
  - **Schedule:** `annealTemp(cyc)` = max(end, start − (cyc−1)·`TD_STEP`), with a 1 °C step. `tdCycles()` = start − end.
  - **Cycle count:** the total stays **≥ `minCycles()`** (tdCycles + 1). The cycles editor can't go below it, and widening the range raises it automatically (never lowers it).
  - **Editor:** item 4 has two phases (`annealPhase`): start, then end. While touchdown is off, the end follows the start (`tdLinked`). `draw_anneal_box()` draws the bar to the start height with a line at the end.
  - **Run title:** "PCR/sqPCR - n of N TD" during the touchdown cycles. The touchdown helpers take the program type: `tdOn(type)`, `tdCycles(type)`, `annealTemp(type,cyc)`, `minCycles(type)`.
- **sqPCR pauses:**
  - **Trigger:** in `PCR_time`, after the Extension of `nextPauseCycle`, `enterPause()` switches to `CASE_Paused`.
  - **Holding:** `CASE_Paused` keeps running `updatePIDTerms()` + `runPID()`, so the block holds the Extension temperature.
  - **Pause screen:** `draw_pause_display()` shows Resume [`pauseN`] (1–9; press to edit, press again to resume), "Run to completion" and "Stop run". `pauseN` defaults to the schedule: second − first at the first pause, then `pauseEvery`. If the user changes it, `userN` keeps their value for the rest of the run.
  - **Resume:** `resumeRun(next)` adds the paused time to `pausedMs`, sets `nextPauseCycle` (0 = no more pauses), then `advanceStep()`.
  - **Next Pause:** the run screen shows it via `estimateSecondsLeft(nextPauseCycle)`.
  - **Paused time** is excluded from Total Time and `lastActualSecs`.
  - **Serial log:** `PAUSE cycle=… elapsed=… block=…` and `RESUME next=…`.
- **Room temperature:** `TEMP_ROOM` = 0, shown as "RT". It's selected by turning a temperature below 25 (editor value 24). The heater stays off, the timer starts at once, and the fan runs while the block is above 30 °C. Works in either program.
- **Auto cool-down** (`CASE_Done`): fan at full until the block stops dropping (< 0.3 °C per 30 s) or 60 s pass (`COOL_TIMEOUT_MS`). Then the bottom line shows a **COMPLETE** bar.
- **Time estimate:** `estimateSecondsLeft()` walks the rest of the program (touchdown temperatures included). It adds ramp time (|ΔT| ÷ rate) plus hold time.
  - Rates come from each step's measured mean if there is one, otherwise the overall rate. Defaults: `HEAT_RATE_DEFAULT` 1.06 / `COOL_RATE_DEFAULT` 0.95 °C/s (measured: 35-cycle run, 5 tubes, stock fan).
  - On an empty block, estimate vs actual was 721 s vs 707 s.
- **Ramp statistics:** `RampStats` in `stats_store` (`STATS_MAGIC` "RAM2"). `step[8]` holds slots 0–4 for PCR **and sqPCR** (same physical ramps) and 5–7 for RT.
  - Each ramp of 3 °C or more updates its step's n, mean, min, max and last values.
  - Also stored: runs started/done, and the last run's estimated vs actual time.
  - Written **once per run** (on entering Done) and printed to serial at 115200.
- **Default programs** (written on the first boot after an upload):
  - **PCR:** 95 °C 5 min / 94 °C 30 s / **63 → 55 °C** 30 s / 72 °C 30 s / 72 °C 5 min, **35** cycles.
  - **sqPCR:** the same steps, **40** cycles, pauses after cycles **1** and **6**, then every **3**.
  - **RT:** Room 10 min / 55 °C 20 min / 85 °C 5 min.

## Open issues and ideas

- **Blocking button waits.** The heater is safe (it's turned off), but the loop still blocks while the button is held. Should become non-blocking.
- **The PID's I term depends on loop speed.** Move it to a fixed `millis()` time step.
- **Stop needs a knob click first.** "Press to STOP" only shows on odd encoder counts.
- **Software SPI to the OLED is slow.** PA22/PA23 can use SERCOM3 hardware SPI.
- **Settings are lost on every upload.** A save/restore around uploads would fix this.
- **Roadmap:**
  - **next:** a device Settings page under Config (temperature calibration offset first; Config then needs a scrolling list)
  - power-failure resume
  - named saved programs (5-character names entered with the knob)
  - an adjustable touchdown step under Config
  - a 6-well block (larger solid centre on the heater PCB, shorter fins). Needs caliper measurements first.
  - a fan upgrade test (4.0 CFM)

**Considered and not planned:**
- **Melt curves:** need in-situ fluorescence, but the owner reads tubes outside the block. If optics are ever added, the unused `LID` (D9 MOSFET) and `TEMP_LID` (A1 on `J4`) could drive an LED and read a photodiode.
- **Gradient and sub-ambient holds:** impossible on this hardware.

## Conventions for agents

- Keep the sketch building with the stock Arduino SAMD / Adafruit SAMD cores and Library Manager libraries. Don't add a build system without discussing it.
- Keep the existing code style (Arduino globals, `#define` state names). Keep changes small, and explain any change to the controller.
- Anything that can drive the heater must fail safe: heater off by default, and whenever the temperature reading is in doubt.
- **Ask before flashing.** Save the stats first (see the flash routine). **Never commit or push the V1.03 dump.**
- Bump `VersionString` for each flashed release, and keep `README.md` (changelog) and this file current.
- Don't modify Gerber or production files under `PCB_Layouts/**/Gerber/` unless asked.
