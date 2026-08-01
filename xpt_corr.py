#!/usr/bin/env python3
"""Correlate TEMP0/X/Y readings against press timing.

Press in short 2s bursts while watching whether TEMP0 (internal sensor)
tracks the press. If TEMP0 tracks presses, the panel IS connected and only
the X/Y/Z channel decode is off. If it ramps monotonically, it's the internal
reference settling (panel open).
"""
import spidev, time

sp = spidev.SpiDev()
sp.open(3, 1)
sp.mode = 0
sp.bits_per_word = 8
sp.max_speed_hz = 200000
try:
    sp.lsbfirst = False
except Exception:
    pass

CMDS = [("TEMP0+", 0x83), ("X-D", 0xD0), ("Y-D", 0x90)]

def rd(cmd):
    rx = sp.xfer2([cmd, 0, 0])
    return (rx[1] << 8 | rx[2]) >> 4

print("=== PRESS ON THE SCREEN in 2s bursts (press, release, press, release...) ===")
t0 = time.time()
while time.time() - t0 < 20.0:
    line = [f"t={time.time()-t0:5.1f}"]
    for label, cmd in CMDS:
        line.append(f"{label}={rd(cmd):4d}")
    print("  " + "  ".join(line), flush=True)
    time.sleep(0.35)
