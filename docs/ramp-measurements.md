# Ramp rate measurements

Real-world heating/cooling rates from this device, used to tune the time estimate (`HEAT_RATE_DEFAULT` / `COOL_RATE_DEFAULT`) and later the controller.

The rates are the time-weighted averages from the Done screen ("Heat:" / "Cool:"), measured from ramps of 3 °C or more.

| Date | Firmware | Load | Run | Heat °C/s | Cool °C/s | Notes |
|---|---|---|---|---|---|---|
| 2026-11-07 | Storey 1.0 (= 0.0.1b) | unknown | 1 cycle | 1.04 | 1.01 | Became the current defaults. Flash stats: Init +69.8 °C @ 1.03, Anneal −38.9 °C @ 1.08, Extension +17.0 °C @ 1.07, Final −16.9 °C @ 0.89 (see `firmware-backup/stats-2026-11-07-0.0.1b.txt`) |
| 2026-11-07 | Spikehole 0.0.3b | **empty (no tubes)** | 4 cycles, touchdown | 1.04 | 1.15 | Stock fan. Flash stats: Init +71.1 °C @ 1.01; Denature @ 1.03 (n=3); Anneal cooling @ 1.15 (n=4, 1.12–1.17); Extension @ 1.09 (n=4). **Estimate 721 s vs actual 707 s (+2%).** See `firmware-backup/stats-2026-11-07-0.0.3b-empty-4cycles.txt` |
| | | 5 tubes | | | | Next: compare with the empty run |

Notes:
- An empty block heats and cools faster than a loaded one (less thermal mass), so the loaded runs are the ones to tune the defaults on.
- The fan upgrade test (4.0 CFM, 0.24 A, vs the stock ~3.45 CFM) is still to do. Compare cooling with the same load.
