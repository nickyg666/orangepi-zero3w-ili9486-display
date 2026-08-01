#!/usr/bin/env python3
"""Live XPT2046 touch poller for the "you touch, I read" session.

Continuously samples the differential touch channels (X, Y, Z1, Z2) at
200kHz on spidev3.1 and prints a line ONLY when a channel's reading changes
by more than a small delta. Also reports the pen-detect estimate (Z1-Z2).

While you touch/draw, watch for channel values to move off their resting
values. Frozen channels == open interconnect; moving == good contact.

Usage:
    sudo python3 xpt_live.py [--hz=200000] [--window=300]
"""
import spidev, time, sys, argparse

DEV = (3, 1)

# differential (SER/DFR=0), PD=11 (powered on, ref on) 12-bit commands
CMDS = {
    "X":  0xD3,
    "Y":  0x93,
    "Z1": 0xB3,
    "Z2": 0xC3,
    "AUX+": 0xE3,   # SE, aliveness ref
    "VBAT+": 0xA3,
    "TEMP0+": 0x83,
}
REST = {"X": 0, "Y": 2047, "Z1": 0, "Z2": 0}

def rd(sp, cmd):
    rx = sp.xfer2([cmd, 0, 0])
    return (rx[1] << 8 | rx[2]) >> 4

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hz", type=int, default=200000)
    ap.add_argument("--window", type=float, default=300.0)
    args = ap.parse_args()

    sp = spidev.SpiDev()
    sp.open(*DEV)
    sp.mode = 0
    sp.bits_per_word = 8
    sp.max_speed_hz = args.hz
    try:
        sp.lsbfirst = False
    except Exception:
        pass

    # baseline
    base = {k: rd(sp, v) for k, v in CMDS.items()}
    print(f"Baseline @{args.hz}Hz: " + " ".join(f"{k}={base[k]}" for k in base), flush=True)
    print("NOW TOUCH/DRAW on the screen. Ctrl+C to stop.", flush=True)

    last = dict(base)
    t0 = time.time()
    while time.time() - t0 < args.window:
        vals = {k: rd(sp, v) for k, v in CMDS.items()}
        changed = [k for k in CMDS if abs(vals[k] - last[k]) > 3]
        if changed:
            ts = time.time() - t0
            dz = vals.get("Z1", 0) - vals.get("Z2", 0)
            msg = "  ".join(f"{k}={vals[k]:4d}" for k in vals)
            print(f"[{ts:7.2f}s] deltaZ1Z2={dz:5d} | {msg}", flush=True)
        last = vals

if __name__ == "__main__":
    main()
