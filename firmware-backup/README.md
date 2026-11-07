# Original device firmware backup

Read from the device on 2026-11-07 with Arduino's bossac 1.7.0 (`-U true -r`), read twice, and the two reads matched.

- `device_original_app.bin` (**local only**: it's git-ignored and isn't in the public repo, because GaudiLabs never released V1.03 or its source. Copies are in this folder on the working machine and in `~/PocketPCR-firmware-backup/`): application flash from 0x2000 to 0x40000 (253,952 bytes). The bootloader at 0x0000–0x1FFF isn't included; bossac can't read it, and it isn't touched by uploads.
  SHA-256 `54c55b6f0eb6bd86433de563ac3add45be16ae09037e4bd90e338d435de63b83`
- **Firmware:** GaudiLabs **V1.03 2022**, newer than the repo's V1.02 2020. It was built with the Adafruit SAMD core 1.7.10 for the **Feather M0** board (USB 239A:800B). The screen text is the same as V1.02.
- **Bootloader:** SAM-BA "v2.0 [Arduino:XYZ] Mar 5 2016" (USB 239A:000B, "Generic CDC"). Not UF2. Chip ATSAMD21G18A, security bit off.
- **Saved program in the dump** (FlashStorage at 0x0E400): Init 94 °C 480 s, Denature 94 °C 30 s, Anneal 55 °C 30 s, Extension 72 °C 90 s, Final 72 °C 420 s, 35 cycles.

## Restore

Put the device in bootloader mode (double-tap reset, or open the port at 1200 baud), then run:

```sh
~/Library/Arduino15/packages/arduino/tools/bossac/1.7.0-arduino3/bossac -U true --port=cu.usbmodemXXXX -e -w -v -R device_original_app.bin
```
