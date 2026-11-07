#!/usr/bin/env python3
"""Decode the ramp statistics (RampStats, "RAM2" layout) from a flash dump.

Usage:  python3 tools/decode_stats.py dump.bin

Make the dump with bossac in bootloader mode (see README "Backing up and flashing"):
  bossac -U true --port=cu.usbmodemXXXX -r dump.bin
The magic value also appears in the program code, so each match is checked for plausible values.
"""
import struct, sys

MAGIC = 0x52414D32  # "RAM2"
NAMES = ["PCR Init", "PCR Denature", "PCR Annealing", "PCR Extension", "PCR Final",
         "RT Incubation", "RT Transcribe", "RT Deactivate"]

def main(path):
    d = open(path, "rb").read()
    for off in range(0, len(d) - 260, 4):
        if struct.unpack_from("<I", d, off)[0] != MAGIC:
            continue
        heat, cool, rh, rc = struct.unpack_from("<ffII", d, off + 4)
        if not (0.05 <= heat <= 10 and 0.05 <= cool <= 10 and rh < 100000 and rc < 100000):
            continue  # magic constant inside program code, not the saved data
        print(f"RampStats at flash 0x{0x2000 + off:05X}")
        o = off + 20
        rows = []
        for name in NAMES:
            n, mean, mn, mx, last_s, last_d = struct.unpack_from("<Ifffff", d, o)
            o += 24
            if n:
                rows.append(f"  {name:14s} n={n:<4d} mean={mean:.3f} min={mn:.3f} max={mx:.3f} C/s"
                            f"  last {last_d:+.1f}C in {last_s:.1f}s")
        started, done, est, act = struct.unpack_from("<IIII", d, o)
        print(f"runs started/completed: {started}/{done}")
        print(f"blended heat/cool: {heat:.3f}/{cool:.3f} C/s   ramps heat/cool: {rh}/{rc}")
        print(f"last completed run estimated/actual: {est}s / {act}s")
        print("\n".join(rows) if rows else "  (no ramps recorded)")
        return
    print("No saved statistics found (none saved since the last upload).")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
