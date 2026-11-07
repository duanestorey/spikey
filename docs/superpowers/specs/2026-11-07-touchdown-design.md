# Touchdown PCR + roadmap — design

Date: 2026-11-07 · Firmware: `code/PocketPCR_V2_quite_dev_i_disp/PocketPCR_V2_quite_dev_i_disp.ino`

## Why

The owner switches often between standard PCR, touchdown PCR and RT incubations. Today the device stores a single fixed program, and the owner edits it by hand for each kind of run. Touchdown is the feature wanted first. Later steps add saved, named programs and a "Hold" program type for RT and enzyme inactivation. **Start small and iterate:** touchdown first, configured on the existing bar-graph screen, with no named programs yet.

Background on touchdown: the anneal temperature starts high, where only the exact target binds, and drops by a fixed step each cycle to a normal anneal temperature. Then the remaining cycles run at that temperature. The early cycles give the correct product a large head start.

## Roadmap (in order)

0. **Groundwork** (before any feature work)
   - [x] Auto cool-down on the Done screen. Compiles; not yet tested on the device. Described in `AGENTS.md`.
   - [x] Install `arduino-cli` and the `arduino:samd` core, plus the Adafruit GFX and SSD1306 libraries. The sketch must compile for `arduino:samd:arduino_zero_native`.
   - [ ] Back up the firmware currently on the device (see `AGENTS.md` / bossac read). This must happen **before the first upload**.
   - [x] Reset run state when a run starts: `PCRcycle=1`, `TEMPi=0`, `TEMPdif_a[]` all zero, `PIDIntegration=false`.
   - [x] Safety cutoff (see below).
   - [x] Fix the out-of-range read: pressing on menu item 12 (Cancel) reads `settings.value[12]` when setting up `counter`, one past the end of the array.
1. **Touchdown**: this document.
2. **Settings item on the main menu.** The main menu becomes three rows: Run PCR / Setup (perhaps renamed "Program") / Settings. Settings would hold the touchdown step size, auto cool-down on/off, left-handed mode and the version. Also a **debug page** showing the saved ramp statistics (`RampStats`: per-step mean/min/max ramp rates, and the last run's estimated vs actual time).
3. **Named saved programs.** Names of up to 5 characters (A–Z, 0–9, `-`), entered with the knob, e.g. `XVITA`, `NZYT`, `NZ-RT`. About 10 programs. Run PCR shows a list to pick from.
4. **Program types.** *PCR* is the current behaviour. *Hold* uses the same 5 bars as 5 steps that each run once, in order. A step with time 0 is skipped, there's no cycle box, and the labels read "Step 1…5". This replaces the owner's current RT workaround (Init = RT, cycles = 1 with den/ann/ext all at 55 °C, Final = 85 °C inactivation).

Done in Spikehole 0.0.1b (branch `storey-1.0`): items 0 (except the firmware backup), plus the version rename and the heater-off button waits.

**Spikehole 0.0.3b** implemented touchdown (roadmap item 1) as designed here, with two refinements. First, **the cycle count is the total, with touchdown included** (the owner found "touchdown added on top" confusing). When touchdown is off and you edit the anneal start, the end follows it, so a plain anneal edit is a single change; you only lower the end in the second press to create a touchdown. (Second refinement.)

**Spikehole 0.0.2b** delivered roadmap items 2 and 4 in a different form from first planned:
- Main menu: Run PCR / Run RT / Config.
- Config: PCR / RT / Debug / Back.
- A separate **RT program type** (Incubation / Transcribe / Deactivate, one pass). It replaces the "Hold" idea.
- A **Room temperature** setting ("RT": heater off, timer starts at once).
- A Debug page for the ramp statistics.

The remaining Settings items (touchdown step size, left-handed mode, cool-down on/off) can go under Config later. **Next: named programs (item 3).**

Later and optional: non-blocking button handling, and a time-based PID.

**Not planned:** a fully flexible list of steps with loops anywhere. The 5-step shape covers every kit the owner uses so far.

## Safety cutoff (groundwork)

If any of the following happens, stop the run: heater PWM 0, fan on, and an error screen ("SENSOR FAULT" / "OVER TEMP") that stays until the button is pressed, then return to the main menu:
- the raw ADC reading is ≤ ~20 or ≥ ~4075 (thermistor shorted or open)
- the calculated temperature is NaN
- the temperature is > 105 °C
- the heater is at full power for 60 s with the temperature rising < 2 °C

Run these checks in every state except the error screen itself. Exact thresholds are the builder's call within these ranges.

- *(Builder's call)* Only count the 60 s "not heating" timer while the block is more than ~5 °C below the target, so a 98–99 °C target the block can barely reach doesn't trip it.
- *(Builder's call)* Turn the heater off before every blocking `while(!digitalRead(butPin));` wait. Otherwise holding the button skips the safety checks with the heater on.

## Touchdown behaviour

**Settings**
- New field `td_start` (°C, int). The existing `settings.value[4]` (anneal temperature) becomes the **anneal end** temperature.
- **Touchdown is on when `td_start > value[4]`.** There's no separate switch.
- Defaults: `td_start = 63`, `value[4] = 55`. This means a freshly flashed device runs touchdown by default, adding 8 cycles. That's intended; set the start equal to the end to turn touchdown off.
- Step size: fixed at **1 °C per cycle**, as a `#define TD_STEP` for now. It moves to Settings in roadmap item 2.

**Cycle schedule**
- Touchdown cycles: `tdCycles = td_start - value[4]`. They come **before** the normal cycle count `value[10]`. With 63 → 55 and 20 cycles you get 8 touchdown cycles at 63, 62 … 56, then 20 cycles at 55, so 28 in total.
- `annealTemp(cycle) = max(value[4], td_start - (cycle-1) * TD_STEP)`, with cycle starting at 1.
- In `PCR_set`: when `PCRstate == 2`, set `TEMPset = annealTemp(PCRcycle)`. Other steps are unchanged.
- End-of-cycle check: `PCRcycle > tdCycles + value[10]`.
- Ramping is unaffected: the anneal step is always reached by cooling from denature, so the ±0.5 °C ramp check isn't an issue.

**Run screen**
- "Set Temp" shows `TEMPset`, the target actually in use, to one decimal. It no longer reads `settings.value[PCRstate*2]`.
- The cycle line shows the total, "Cycle: 3 of 28", with " TD" added while `PCRcycle <= tdCycles`.

**Setup screen (bar graph)**
- **The Annealing bar shows a range.** Its outline height comes from `td_start`, and a horizontal line across it marks `value[4]`, using the same y-formula as the bar top (`y+2-val/2`). When the two are equal, it draws as a normal bar.
- **The bar label** shows `td_start`. While the end is being edited, it shows `value[4]` instead.
- **Header (top line) while Annealing temp is selected:** "Anneal 63>55 [°C]" when touchdown is on, otherwise the existing "Annealing Temp. [°C]". Use whatever arrow glyph the default font has, or `>`.
- **Edit flow** for menu item 4 (Annealing temp):
  1. Press → edit the **top** (`td_start`). The header reads "TD start [°C]" and the bar's height follows the knob.
  2. Press → edit the **bottom** (`value[4]`). The header reads "Anneal end [°C]" and the line moves.
  3. Press → back to browsing (`CASE_Settings`).
- **Limits:** `td_start` 25–99, and `value[4]` 25–`td_start`. If `td_start` is lowered below `value[4]`, `value[4]` follows it down.
- *(Builder's call)* The "Nx" cycles box keeps showing `value[10]`, the normal cycle count. The run screen shows the total including touchdown.
- **Menu numbering is unchanged:** items 0–12 stay exactly as they are, because both values live on item 4. This keeps the hard-coded 10/11/12 checks intact.

**Saved settings**
- Extend `EEprom` with `int td_start;` and `int magic;`. The `value[12]` array stays as it is, with `value[11]` unused from now on.
- On boot, if `magic != SETTINGS_MAGIC`, write all the defaults (existing ones plus `td_start = 63`) and the magic value. Flash reads as zeros after every upload, so this also covers a fresh flash. Saved settings resetting on upload is expected and already happens.
- Save and Cancel keep working as now: Set writes the whole struct, Cancel re-reads it.

## Constraints

- Keep the code building with the stock Arduino SAMD core and Library Manager libraries. Keep the existing code style (globals, `#define` state names).
- Anything that drives the heater must fail safe.
- PocketPCR has **no lid heater**. D9 and A1 are unused on purpose.
- There's no compiler or hardware in the loop on the agent side, apart from `arduino-cli compile`. The owner tests on the device.

## Left to the builder

- Exact pixel positions and the arrow glyph.
- Debounce details.
- How the safety-check helpers are laid out.
- Whether `annealTemp`/`tdCycles` are functions or are computed inline. Keeping them as pure functions with no hardware calls is preferred.
