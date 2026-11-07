# Spikey

**Spikey** is an alternative firmware for the **[PocketPCR](http://gaudi.ch/PocketPCR/)**, the pocket-size, USB-powered PCR thermocycler designed by [GaudiLabs](http://www.gaudi.ch/GaudiLabs/). It runs on the stock PocketPCR hardware and adds:

- touchdown PCR
- a separate RT (reverse transcription) program
- safety cutoffs
- a time-remaining estimate
- run summaries
- an automatic cool-down

**Coming in 1.1.0:** **sqPCR** (semi-quantitative PCR), a PCR program with **reading pauses** for manual fluorescence readings. It's written but switched off in 1.0 builds; see [sqPCR (coming in 1.1.0)](#sqpcr-coming-in-110).

This repository is a fork of [GaudiLabs/PocketPCR](https://github.com/GaudiLabs/PocketPCR). All the hardware design files (PCBs, BOM, block drawing) are GaudiLabs' work and are included unchanged. Please support the original project and open science hardware at [gaudishop.ch](https://gaudishop.ch/).

> **Status:** **1.0.0**, the first stable release. **sqPCR** is planned for 1.1.0. `master` holds the latest Spikey release. GaudiLabs' original firmware (V1.02) is kept as the tag `gaudilabs-v1.02`.

> **Use at your own risk.** Spikey is an unofficial, hobbyist firmware, provided **without any warranty**. It isn't a medical or diagnostic device. See [Safety and disclaimer](#safety-and-disclaimer).

## Features

- **Two programs**, each with its own saved settings:
  - **PCR:** Init → n × [Denature → Annealing → Extension] → Final extension
  - **RT:** Incubation → Transcribe → Deactivate, one pass
- **Touchdown PCR:** set a start and an end temperature on the Annealing bar, and the anneal temperature drops 1 °C per cycle until it reaches the end. The cycle count is the total, touchdown included. It can't be set lower than a full touchdown needs, and widening the touchdown raises it automatically.
- **Room temperature ("RT") steps:** turn a temperature down past 25 °C until it shows **RT**. The heater stays off and the step's timer starts immediately. Good for room-temperature incubations, which the fan alone can't hold reliably.
  - **Two meanings of "RT":** in the editor, RT on a *temperature* means room temperature. **Run RT** and **RT Program** are the *reverse transcription* program. The default RT program starts with a room-temperature step, so you'll see both.
  - **Use RT, not 25 °C,** for room-temperature steps. The block has no active cooling below room temperature, and a step's timer only starts once the block is within 0.5 °C of its target. In a room warmer than about 25 °C, a 25 °C step would wait indefinitely.
- **Run screen:** cycle count (with "TD" during touchdown), step, target and block temperature, time left in the step, elapsed time, and **estimated time remaining**. The estimate learns this block's real heating and cooling rates.
- **Done screen:** total time vs the estimate, this run's heating and cooling rates, then an **automatic cool-down** (fan for up to 60 s) and a **COMPLETE** banner.
- **Safety cutoff:** heater off and fan on, with an error screen, if the temperature sensor fails ("SENSOR ERR"), the block overheats above 105 °C ("OVER TEMP"), or the heater runs flat out without warming the block ("NO HEATING").
- **Debug pages:** saved ramp-rate statistics for each step, plus the last run's estimated vs actual time.

## Using it

| Screen | Items |
|---|---|
| **Main Menu** | Run PCR · Run RT · Config > |
| **Config** | PCR Program · RT Program · Debug · About · < Back (About shows the start-up splash with the version) |

- **Turn** the knob to move and **press** to select.
- **In a program editor:**
  1. Press to edit the selected value, turn to change it, then press again.
  2. Move to **Set** to save, or **Cancel** to discard.
  3. Times above 90 s switch to minutes.
- **Touchdown:** on the PCR Annealing bar:
  1. The first press edits the **start** temperature.
  2. The second press edits the **end** temperature (shown as a line across the bar).
  3. The third press finishes.

  When the start and end are equal, touchdown is off.
- **Stop a run:** turn the knob one click, then press "Press to STOP".
- **Left-handed mode:** hold the button during the 5-second start-up splash to rotate the screen.

**Default programs** (loaded after every firmware upload):

| Step | PCR |
|---|---|
| Init | 95 °C, 5 min |
| Denature | 94 °C, 30 s |
| Annealing | 63 → 55 °C (touchdown), 30 s |
| Extension | 72 °C, 30 s |
| Final extension | 72 °C, 5 min |
| Cycles | 35 (8 touchdown + 27) |

**RT:** Incubation Room 10 min → Transcribe 55 °C 20 min → Deactivate 85 °C 5 min.

## sqPCR (coming in 1.1.0)

**sqPCR** (semi-quantitative PCR) is the same as PCR, plus pauses for manual fluorescence readings. It finds roughly where each sample's curve rises (within a cycle or two), using an external fluorometer. It isn't a replacement for a real-time qPCR instrument.

It's finished but **switched off in 1.0 builds** and will ship in **1.1.0**. To try it now, set `#define ENABLE_SQPCR 1` near the top of the sketch and flash. "Run sqPCR" and "sqPCR Program" then appear in the menus (Config gets 6 items and scrolls).

- **Reading pauses:** the run pauses after Extension at chosen cycles and **holds at the Extension temperature** while you read the tubes. On the pause screen you choose **Resume [N cycles]** (change N during the run, e.g. to 1–2 once fluorescence starts rising), **Run to completion** (plateau found) or **Stop run**. The run screen shows **Next Pause:** as an estimated countdown to the next reading.
- **Setting the pauses:** in Config → sqPCR Program. The cycles box reads e.g. `40x P1/6/3`: 40 cycles in total, a **baseline pause after cycle 1**, a **second pause after cycle 6**, then **every 3** cycles. Each part is its own item, and the pause numbers are 1–9. During a pause:
  - **Resume [N cycles]:** press to change N (1–9), turn, then press again to resume. N follows the schedule (5 after the cycle-1 pause, then 3) unless you change it; a changed N is kept for the rest of the run.
  - **Run to completion:** runs to the end without stopping again.
  - **Stop run:** ends the run.

  The block holds at the Extension temperature throughout, and the bottom lines show the hold temperature with the live block temperature ("Hold: 72°C / 71.9°C") and the time paused.
- **Tips:**
  - Use an oil overlay; there's no heated lid.
  - Read tubes one at a time, taking each out and reading it **promptly and at the same pace every time**, because the dye's brightness changes as the tube cools.
  - Normalise each tube's readings to its own baseline and plateau, and compare where the curves rise.
  - Include a no-template control (NTC).
  - Spacing: amplification curves typically rise from baseline to plateau over only **6–7 cycles**, so pausing every 3 gives 2–3 points in the rise. The cycle-1 pause gives a **baseline** reading at the same temperature and chemistry as all later reads, and cycles 6, 9 and 12 add more baseline points before a typical rise. Once fluorescence starts climbing, drop Resume to 1–2 cycles.
  - The serial log (115200 baud) records each pause's cycle and time.

**Default sqPCR program:** the PCR steps above, **40** cycles (8 touchdown + 32), pauses after cycles **1** and **6**, then every **3** (`P1/6/3`).

## Backing up and flashing

**Uploading firmware erases the saved programs and statistics.** Always back up the firmware that's on your device first, so you can return to it.

### What you need

- **Cable:** a PocketPCR and a **USB-C data cable** (some cables are charge-only).
- **Computer:** macOS, Linux or Windows.
- **[arduino-cli](https://arduino.github.io/arduino-cli/) or the Arduino IDE** (to build from source; the prebuilt file only needs `bossac`), with:
  - the **Arduino SAMD** and **Adafruit SAMD** board packages. Adafruit's needs this board manager URL: `https://adafruit.github.io/arduino-board-index/package_adafruit_index.json`
  - the **Adafruit GFX Library** and **Adafruit SSD1306** libraries
- **`bossac`**, which comes with the Arduino SAMD package. It's used for the backup and restore. It isn't on your `PATH`, so use its full path (or add its folder to `PATH`):
  - macOS: `~/Library/Arduino15/packages/arduino/tools/bossac/1.7.0-arduino3/bossac`
  - Linux: `~/.arduino15/packages/arduino/tools/bossac/1.7.0-arduino3/bossac`
  - Windows: `%LOCALAPPDATA%\Arduino15\packages\arduino\tools\bossac\1.7.0-arduino3\bossac.exe`

  The commands below just say `bossac`.
- **Recovery only (optional):** an SWD programmer (J-Link, Atmel-ICE or CMSIS-DAP) on header `J2`, to re-install the bootloader on a bricked board.

One-time setup with arduino-cli:
```sh
arduino-cli core install arduino:samd adafruit:samd \
  --additional-urls https://adafruit.github.io/arduino-board-index/package_adafruit_index.json
arduino-cli lib install "Adafruit GFX Library" "Adafruit SSD1306"
```

The PocketPCR uses an **ATSAMD21G18A**. It shows up as an **Adafruit Feather M0** (USB `239A:800B`), and its bootloader appears as `239A:000B`. The serial port is something like `/dev/cu.usbmodem101` (macOS), `/dev/ttyACM0` (Linux) or `COM3` (Windows).

### 1. Back up the current firmware

Put the device into its bootloader by opening the serial port at 1200 baud:
```sh
stty -f /dev/cu.usbmodem101 1200     # macOS  (Linux: stty -F /dev/ttyACM0 1200)
```
On **Windows** (Command Prompt), with your port number:
```bat
mode COM3: BAUD=1200 PARITY=N DATA=8 STOP=1
```
If that doesn't work, this Python one-liner (macOS/Linux) does the same thing:
```sh
python3 -c "import os,termios;fd=os.open('/dev/cu.usbmodem101',os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK);a=termios.tcgetattr(fd);a[4]=a[5]=termios.B1200;termios.tcsetattr(fd,termios.TCSANOW,a);os.close(fd)"
```
In bootloader mode the screen goes blank, and the USB ID changes to `239A:000B`. The port name usually stays the same on macOS and Linux; on Windows it may get a **new COM number** (check Device Manager → Ports).

> **About "1200 baud":** this is only a signal, not the transfer speed. Opening and closing the port at 1200 baud tells the firmware to reboot into its bootloader. The PocketPCR uses native USB, so the baud rate is ignored and reads and writes run at full USB speed. A full backup takes about 10 s, and flashing plus verifying takes about 7 s.

Then read the whole application area, 248 KB above the 8 KB bootloader. Read it twice and compare. `--port` takes the port name: `cu.usbmodem101` on macOS, `ttyACM0` on Linux, `COM3` on Windows.
```sh
bossac -U true --port=cu.usbmodem101 -r pocketpcr_backup.bin
bossac -U true --port=cu.usbmodem101 -r verify.bin && cmp pocketpcr_backup.bin verify.bin && echo OK
```
(On Windows, compare with `fc /b pocketpcr_backup.bin verify.bin`.)

The file should be **253,952 bytes**. Keep it somewhere safe. The bootloader isn't included, but uploads never touch it. You can go straight on to step 2 from here. If you don't, **unplug and replug** the device to leave the bootloader; a software reset sends it straight back in.

Optional: `python3 tools/decode_stats.py pocketpcr_backup.bin` shows the ramp statistics saved by Spikey.

### 2. Flash Spikey

**Option A: the prebuilt file** (no Arduino setup needed, just `bossac`). Download `spikey-1.0.0.bin` from the [1.0.0 release](https://github.com/duanestorey/spikey/releases/tag/spikey-1.0.0), and check it before flashing:

```sh
shasum -a 256 spikey-1.0.0.bin      # Linux: sha256sum   Windows: certutil -hashfile spikey-1.0.0.bin SHA256
```
It must print **`289b3a144270bca58213d663c2ddf88a5af6139c7473098d59535f549926e744`**. If it doesn't, don't flash it.

With the device in its bootloader (step 1), flash and verify it:
```sh
bossac -U true --port=cu.usbmodem101 -e -w -v -R spikey-1.0.0.bin
```
This is for the `bossac` 1.7 that ships with the Arduino SAMD package, which writes above the bootloader automatically. Newer `bossac` versions (1.8 and up) need `--offset=0x2000` added. **Never** write at offset 0: that would overwrite the bootloader. Unplug and replug the device afterwards.

**Option B: build it yourself** from the source (the same code as the `spikey-1.0.0` tag), if you'd rather not run a prebuilt file:

```sh
arduino-cli compile --fqbn adafruit:samd:adafruit_feather_m0 --upload -p /dev/cu.usbmodem101 \
  code/PocketPCR_V2_quite_dev_i_disp
```
Use `-p /dev/ttyACM0` on Linux or `-p COM3` on Windows.

The upload resets the device into the bootloader automatically. Wait for **"Verify successful"**. The device then restarts and shows the splash with the Spikey version.

In the Arduino IDE: select the board **Adafruit Feather M0** and the right port, open `code/PocketPCR_V2_quite_dev_i_disp/PocketPCR_V2_quite_dev_i_disp.ino`, and click Upload.

### 3. Restore a backup

Put the device in its bootloader (step 1), then:
```sh
bossac -U true --port=cu.usbmodem101 -e -w -v -R pocketpcr_backup.bin
```
Unplug and replug the device afterwards.

### Troubleshooting

- **No port appears:** try another cable, and check it carries data. Replugging may help.
- **Device stays in the bootloader** (blank screen; shows up as `239A:000B`): unplug and replug it.
- **Bricked board, no bootloader:** use an SWD programmer on `J2` and "Burn Bootloader" for the Feather M0 or Arduino Zero.

## Repository layout

| Path | Contents |
|---|---|
| `code/PocketPCR_V2_quite_dev_i_disp/` | Firmware (Arduino sketch) |
| `tools/` | Helper scripts (stats decoder) |
| `docs/` | Design notes and ramp-rate measurements |
| `firmware-backup/` | Notes on the original firmware backup and saved stats |
| `Parts/` | Heater block drawing (GaudiLabs) and a 3D model of the block (by Andre Maia Chagas, see `Parts/info.md`) |
| `PCB_Layouts/`, `PocketPCR_BOM.ods` | GaudiLabs' PCB designs and bill of materials |
| `AGENTS.md` | Technical notes for developers and AI agents |

## Changelog

### 1.1.0 (planned)

**New**
- **sqPCR program for manual fluorescence readings** (already in the code behind `ENABLE_SQPCR`):
  - pauses after Extension at chosen cycles, holding the Extension temperature
  - a pause screen with **Resume [N cycles]** (adjustable mid-run), **Run to completion** and **Stop run**
  - a **Next Pause** countdown on the run screen
  - pause and resume events logged over USB serial
  - paused time left out of the run statistics

### 1.0.0 (2026-11-07)

First stable Spikey release. Compared with GaudiLabs' PocketPCR firmware V1.02:

**New**
- **PCR and RT programs**, each with its own saved settings. RT is Incubation → Transcribe → Deactivate, one pass.
- **Touchdown PCR** on the Annealing step: a start/end range on the bar, 1 °C per cycle. The cycle count is the total, kept at least as long as a full touchdown needs.
- **Room temperature ("RT") steps:** heater off, and the timer starts immediately.
- **Menus:** Main Menu (Run PCR / Run RT / Config >) and Config (PCR Program / RT Program / Debug / About / < Back), in a clean small-font style.
- **Run screen:** cycle count with a touchdown marker, step, target and block temperature, step time, elapsed time and **estimated time remaining**.
- **Time estimate** that learns this block's real heating and cooling rates for each step. Measured within about 2% on an empty block.
- **Done screen** with total vs estimated time and this run's heating and cooling rates, plus an **automatic cool-down** and a **COMPLETE** banner.
- **Ramp statistics** saved in flash (once per run, to spare it), shown on the **Debug** pages and printed over USB serial.
- **Splash screen** (5 s) showing the firmware version.
- **Sensible default programs** after every upload.

**Safety and fixes**
- **Safety cutoff** for sensor failure, over-temperature (105 °C) and "heater on but not warming", with an error screen.
- **Heater off** whenever the firmware waits for the button. Previously, holding the button froze the heater at its last power level.
- **Back-to-back runs fixed:** a second run in the same session used to do only one cycle.
- **Out-of-range memory read** when pressing Cancel in the editor, fixed.
- **Step times capped at 99 min,** so they no longer overflow the editor box ("100m").
- **Unsaved edits are discarded** if an error interrupts the editor, so the next run uses the saved program.

**Build**
- Built for the **Adafruit Feather M0** board setting, matching the PocketPCR's shipped firmware.

### Beta history

| Version | Highlights |
|---|---|
| 0.0.8b | sqPCR switched off for 1.0 (build flag `ENABLE_SQPCR`, planned for 1.1.0); menus show PCR and RT only |
| 0.0.7b | Config > About (shows the splash and version again); menus longer than 5 items scroll |
| 0.0.6b | sqPCR program with manual reading pauses (Resume [N], Run to completion, Next Pause countdown; P1/6/3 schedule); 5-item Config menu; renamed to Spikey |
| 0.0.5b | Default programs updated (PCR 95/94/63→55/72/72, 35 cycles; RT Room/55 °C 20 min/85 °C) |
| 0.0.4b | COMPLETE banner after the cool-down |
| 0.0.3b | Touchdown PCR; cycle count is the total and covers a full touchdown |
| 0.0.2b | PCR + RT programs, Config/Debug menus, Room temperature, small-font menus, renamed to Spikehole |
| 0.0.1b | Safety cutoff, auto cool-down, new run and Done screens, time estimate, ramp statistics, run-state fixes (released as "Storey") |

## Credits

- **PocketPCR hardware and original firmware:** [GaudiLabs](http://www.gaudi.ch/GaudiLabs/) (Urs Gaudenz). Project page: <http://gaudi.ch/PocketPCR/>
- **3D model of the heater block:** Andre Maia Chagas ([@amchagas](https://github.com/amchagas)). See `Parts/info.md`.
- **Spikey firmware changes:** Duane Storey.

## Safety and disclaimer

Spikey is free software, provided **"as is", without warranty of any kind**, express or implied, including fitness for a particular purpose. By installing it, **you use it entirely at your own risk**. This is the same as GPL-3.0 sections 15 and 16 (see `LICENSE`): the author isn't liable for any damage, injury, loss of samples, data or equipment, or any other consequence of using it.

- **Not a medical or diagnostic device.** Spikey is for education, hobby and research use only. Don't use it for clinical, diagnostic, food-safety or any other decision where a wrong result matters.
- **Hardware by GaudiLabs, firmware by Spikey.** The PocketPCR hardware is designed and made by [GaudiLabs](http://www.gaudi.ch/GaudiLabs/). Spikey is **firmware only**, based on a fork of GaudiLabs' published open-source PocketPCR firmware (V1.02). Questions about the hardware itself go to GaudiLabs; problems with Spikey go to this repository's issues, not to GaudiLabs.
- **Unofficial.** Spikey isn't made, endorsed or supported by GaudiLabs. Installing it replaces the factory firmware, and may affect any support or warranty you have from the maker. Back up your original firmware first (see [Backing up and flashing](#backing-up-and-flashing)).
- **It controls a heater.** The block reaches about 100 °C. The firmware has safety cutoffs (sensor failure, over-temperature, heater not warming), but they can't cover every fault. Don't leave a run unattended where an overheating device could cause harm. Keep it on a non-flammable surface, away from anything that can melt or burn, and unplug it if anything looks or smells wrong.
- **Results aren't guaranteed.** Temperatures come from a single block thermistor and aren't calibrated against a reference. Check important protocols with your own controls.
- **Lab safety is up to you,** including biological material, reagents, gel stains and UV light.

## License

PocketPCR, all design files and software are licensed under the **GNU General Public License v3.0**. Spikey is distributed under the same license. See `LICENSE`.
