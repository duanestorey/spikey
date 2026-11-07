# Spikehole

**Spikehole** is an alternative firmware for the **[PocketPCR](http://gaudi.ch/PocketPCR/)**, the pocket-size, USB-powered PCR thermocycler designed by [GaudiLabs](http://www.gaudi.ch/GaudiLabs/). It runs on the stock PocketPCR hardware and adds:

- touchdown PCR
- a separate RT (reverse transcription) program
- safety cutoffs
- a time-remaining estimate
- run summaries
- an automatic cool-down

This repository is a fork of [GaudiLabs/PocketPCR](https://github.com/GaudiLabs/PocketPCR). All the hardware design files (PCBs, BOM, block drawing) are GaudiLabs' work and are included unchanged. Please support the original project and open science hardware at [gaudishop.ch](https://gaudishop.ch/).

> **Status:** beta (`Spikehole 0.0.Nb`), working towards **1.0.0**. Development happens on the `1.0.0` branch. `master` is GaudiLabs' original firmware (V1.02).

## Features

- **Two programs:** **PCR** (Init → n × [Denature → Annealing → Extension] → Final extension) and **RT** (Incubation → Transcribe → Deactivate, one pass). Each has its own saved settings.
- **Touchdown PCR:** set a start and an end temperature on the Annealing bar, and the anneal temperature drops 1 °C per cycle until it reaches the end. The cycle count is the total, touchdown included. It can't be set lower than a full touchdown needs, and widening the touchdown raises it automatically.
- **Room temperature ("RT") steps:** turn any temperature below 25 °C. The heater stays off and the step's timer starts immediately. Good for room-temperature incubations, which the fan alone can't hold reliably.
- **Run screen:** cycle count (with "TD" during touchdown), step, target and block temperature, time left in the step, elapsed time, and **estimated time remaining**. The estimate learns this block's real heating and cooling rates.
- **Done screen:** total time vs the estimate, this run's heating and cooling rates, then an **automatic cool-down** (fan for up to 60 s) and a **COMPLETE** banner.
- **Safety cutoff:** heater off and fan on, with an error screen, if the temperature sensor fails ("SENSOR ERR"), the block overheats above 105 °C ("OVER TEMP"), or the heater runs flat out without warming the block ("NO HEATING").
- **Debug pages:** saved ramp-rate statistics for each step, plus the last run's estimated vs actual time.

## Using it

| Screen | Items |
|---|---|
| **Main Menu** | Run PCR · Run RT · Config > |
| **Config** | PCR program · RT program · Debug · < Back |

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

| PCR | | RT | |
|---|---|---|---|
| Init | 95 °C, 5 min | Incubation | Room, 10 min |
| Denature | 94 °C, 30 s | Transcribe | 55 °C, 20 min |
| Annealing | 63 → 55 °C (touchdown), 30 s | Deactivate | 85 °C, 5 min |
| Extension | 72 °C, 30 s | | |
| Final extension | 72 °C, 5 min | | |
| Cycles | 35 (8 touchdown + 27) | | |

## Backing up and flashing

**Uploading firmware erases the saved programs and statistics.** Always back up the firmware that's on your device first, so you can return to it.

### What you need

- **Cable:** a PocketPCR and a **USB-C data cable** (some cables are charge-only).
- **Computer:** macOS, Linux or Windows.
- **[arduino-cli](https://arduino.github.io/arduino-cli/) or the Arduino IDE**, with:
  - the **Arduino SAMD** and **Adafruit SAMD** board packages. Adafruit's needs this board manager URL: `https://adafruit.github.io/arduino-board-index/package_adafruit_index.json`
  - the **Adafruit GFX Library** and **Adafruit SSD1306** libraries
- **`bossac`**, which comes with the Arduino SAMD package (e.g. `~/Library/Arduino15/packages/arduino/tools/bossac/1.7.0-arduino3/bossac` on macOS, `~/.arduino15/...` on Linux). It's used for the backup.
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
If the port doesn't change, this Python one-liner (macOS/Linux) does the same thing:
```sh
python3 -c "import os,termios;fd=os.open('/dev/cu.usbmodem101',os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK);a=termios.tcgetattr(fd);a[4]=a[5]=termios.B1200;termios.tcsetattr(fd,termios.TCSANOW,a);os.close(fd)"
```
In bootloader mode the screen goes blank, and the USB ID changes to `239A:000B` (the port name usually stays the same).

Then read the whole application area, 248 KB above the 8 KB bootloader. Read it twice and compare:
```sh
bossac -U true --port=cu.usbmodem101 -r pocketpcr_backup.bin
bossac -U true --port=cu.usbmodem101 -r verify.bin && cmp pocketpcr_backup.bin verify.bin && echo OK
```

The file should be **253,952 bytes**. Keep it somewhere safe. The bootloader isn't included, but uploads never touch it. Afterwards, **unplug and replug** the device to leave the bootloader; a software reset sends it straight back in.

Optional: `python3 tools/decode_stats.py pocketpcr_backup.bin` shows the ramp statistics saved by Spikehole.

### 2. Build and flash Spikehole

```sh
arduino-cli compile --fqbn adafruit:samd:adafruit_feather_m0 --upload -p /dev/cu.usbmodem101 \
  code/PocketPCR_V2_quite_dev_i_disp
```

The upload resets the device into the bootloader automatically. Wait for **"Verify successful"**. The device then restarts and shows the splash with the Spikehole version.

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

### 1.0.0 (unreleased)

First stable Spikehole release. Compared with GaudiLabs' PocketPCR firmware V1.02:

**New**
- **PCR and RT programs**, each with its own saved settings. RT is Incubation → Transcribe → Deactivate, one pass.
- **Touchdown PCR** on the Annealing step: a start/end range on the bar, 1 °C per cycle. The cycle count is the total, kept at least as long as a full touchdown needs.
- **Room temperature ("RT") steps:** heater off, and the timer starts immediately.
- **Menus:** Main Menu (Run PCR / Run RT / Config >) and Config (PCR program / RT program / Debug / < Back), in a clean small-font style.
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

**Build**
- Built for the **Adafruit Feather M0** board setting, matching the PocketPCR's shipped firmware.

### Beta history

| Version | Highlights |
|---|---|
| 0.0.5b | Default programs updated (PCR 95/94/63→55/72/72, 35 cycles; RT Room/55 °C 20 min/85 °C) |
| 0.0.4b | COMPLETE banner after the cool-down |
| 0.0.3b | Touchdown PCR; cycle count is the total and covers a full touchdown |
| 0.0.2b | PCR + RT programs, Config/Debug menus, Room temperature, small-font menus, renamed to Spikehole |
| 0.0.1b | Safety cutoff, auto cool-down, new run and Done screens, time estimate, ramp statistics, run-state fixes (released as "Storey") |

## Credits

- **PocketPCR hardware and original firmware:** [GaudiLabs](http://www.gaudi.ch/GaudiLabs/) (Urs Gaudenz). Project page: <http://gaudi.ch/PocketPCR/>
- **3D model of the heater block:** Andre Maia Chagas ([@amchagas](https://github.com/amchagas)). See `Parts/info.md`.
- **Spikehole firmware changes:** Duane Storey.

## License

PocketPCR, all design files and software are licensed under the **GNU General Public License v3.0**. Spikehole is distributed under the same license. See `LICENSE`.
