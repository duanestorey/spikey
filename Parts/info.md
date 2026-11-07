# Parts

## `PocketPCRHeater_V1.PDF`: original block drawing (GaudiLabs)

A dimensioned 2D drawing of the aluminium heater block by **Urs Gaudenz / GaudiLabs**, from the upstream [GaudiLabs/PocketPCR](https://github.com/GaudiLabs/PocketPCR) repository. Licensed GPL-3.0, like the rest of the project.

Key dimensions:
- **Block:** Ø19.50 × 7.10 mm, aluminium, ±0.1 mm (ISO 2768)
- **Wells:** 5, on a 6.70 mm radius, 17.5° taper, R1.40 bottom, 0.40 mm floor
- **Centre:** M3 tapped hole
- **Top edge:** 0.30 × 45° chamfer

## 3D model of the heater block, by Andre Maia Chagas

| File | What it is |
|---|---|
| `aluminium_heater_block.FCStd` | FreeCAD source model |
| `aluminium_heater_block-final_block_v1.step` | STEP export (CAD/CAM) |
| `aluminium_heater_block-final_block_v1.3mf` | 3MF mesh export (slicers / viewers) |

**Credit:** these files were made by **Andre Maia Chagas** ([@amchagas](https://github.com/amchagas)) and are copied unmodified from his fork, [amchagas/PocketPCR](https://github.com/amchagas/PocketPCR):
- Commit [`a4be64e`](https://github.com/amchagas/PocketPCR/commit/a4be64e478c2525ef6594c22cf0edb97fa28bf65), 2026-07-01, "first punt at making a freecad model of the heating block"
- Copied 2026-11-07. His FreeCAD autosave backup (`.FCBak`) was left out.

Thank you, Andre!

**License:** GPL-3.0. His fork carries the GPL-3.0 `LICENSE` and the upstream README statement that "PocketPCR, all design files and software are licensed under a GNU GENERAL PUBLIC LICENSE". The model is also a derivative of the GPL-licensed GaudiLabs drawing above.

The `.FCStd` file's internal document metadata says "All rights reserved". That is FreeCAD's default for new documents and conflicts with the license the files were published under, so it is very likely unintentional. We follow the repository's GPL-3.0 license. If the author indicates otherwise, these files should be removed or relicensed as he asks.

**Status:**
- The author describes it as a "first punt", so treat it as unverified.
- The 3MF's overall size, **Ø19.50 × 7.10 mm**, matches the drawing.
- Well positions, taper and depth have **not** been checked against the drawing. Check them before machining.

**SHA-256 at the time of copying:**
```
213ebd56ec172bd3993c71f75c1567f7a6fe4cb138ba6f40d3e20e964c6881a6  aluminium_heater_block.FCStd
9b65f069b6055ef190470601641da4b2080c1fb2b90776542c2975e1df09306c  aluminium_heater_block-final_block_v1.step
4461923f55094ce1b6dd74b1cddfa91fa69bc578a460fb548bf89781b34e0de7  aluminium_heater_block-final_block_v1.3mf
```
