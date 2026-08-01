#!/usr/bin/env python3
"""XPT2046 corrected probe.

The old probe used SER (single-ended) control bytes for X/Y/Z, but touch
channels REQUIRE differential mode (SER/DFR=0). This probe tests:

  * DFR touch commands  (12-bit: 0xD0 X, 0x90 Y, 0xB0 Z1, 0xC0 Z2)
  * DFR touch commands  ( 8-bit: 0xD8 X, 0x98 Y, 0xB8 Z1, 0xC8 Z2)
  * DFR with PD=11 keep-on (0xD3/0x93/0xB3/0xC3)
  * SE aliveness channels (must be SE): AUX=0xE0, VBAT=0xA0, TEMP0=0x80,
    TEMP1=0xF0  --  if the chip is powered+alive, VBAT~VCC/4 and TEMP~600mV
    give distinct non-mid-scale values; a dead chip/open MISO gives the same
    frozen value on every channel.

Usage:  PRESS AND DRAW continuously on the screen for the whole window.
"""
import spidev, time, sys

DEV = (3, 1)
WINDOW = 20.0

CHANNELS = [  # (label, 12-bit cmd, 8-bit cmd)
    ("X-D  ", 0xD0, 0xD8),
    ("Y-D  ", 0x90, 0x98),
    ("Z1-D ", 0xB0, 0xB8),
    ("Z2-D ", 0xC0, 0xC8),
    ("X-D11", 0xD3, None),
    ("Y-D11", 0x93, None),
    ("Z1-D1", 0xB3, None),
    ("Z2-D1", 0xC3, None),
    ("AUX+ ", 0xE3, None),   # SE PD=11 (ref on, always powered) 0xE3=1110 0011
    ("VBAT+", 0xA3, None),   # SE PD=11
    ("TEMP0+", 0x83, None),  # SE PD=11
    ("TEMP1+", 0xF3, None),  # SE PD=11
    ("AUX00", 0xE0, None),   # SE PD=00 (ref off - control)
    ("VBAT0", 0xA0, None),   # SE PD=00
    ("TEMP00", 0x80, None),  # SE PD=00
    ("TEMP10", 0xF0, None),  # SE PD=00
]

def xfer(sp, cmd):
    rx = sp.xfer2([cmd, 0, 0])
    return rx[0], rx[1], rx[2]

def main():
    sp = spidev.SpiDev()
    sp.open(*DEV)
    sp.mode = 0
    sp.bits_per_word = 8
    sp.max_speed_hz = 200000
    try:
        sp.lsbfirst = False
    except Exception:
        pass

    print("=== RAW bit pattern (first 2 reads, 24-bit frame [cmd,0,0]) ===")
    for label, c12, c8 in CHANNELS:
        for cmd in ([c12, c8] if c8 else [c12]):
            r0, r1, r2 = xfer(sp, cmd)
            bits = f"{r1:08b} {r2:08b}"
            print(f"  {label} cmd=0x{cmd:02X}: rx[0]=0x{r0:02X} rx[1]=0x{r1:02X} rx[2]=0x{r2:02X}  MISO={bits}")

    print(f"\n=== PRESS AND DRAW ON THE SCREEN for the full {WINDOW:.0f}s window ===")
    stats = {}
    t0 = time.time()
    n = 0
    while time.time() - t0 < WINDOW:
        for label, c12, c8 in CHANNELS:
            for name, cmd in (("12", c12), ("8", c8)):
                if cmd is None:
                    continue
                r0, r1, r2 = xfer(sp, cmd)
                v = (r1 << 8 | r2) >> 4
                key = (label, name)
                s = stats.setdefault(key, {"n": 0, "min": 0xFFFF, "max": 0, "distinct": set()})
                s["n"] += 1
                if v < s["min"]: s["min"] = v
                if v > s["max"]: s["max"] = v
                s["distinct"].add(v)
                if len(s["distinct"]) > 8192:
                    s["distinct"].pop()
        n += 1
        if n % 2000 == 0:
            print(f"  ... {time.time()-t0:5.1f}s / {WINDOW:.0f}s", flush=True)

    print("\n=== RESULTS (under press) ===")
    print(f"{'channel':8} {'bits':4} {'n':>8} {'min':>5} {'max':>5} {'range':>6} {'distinct':>8}  verdict")
    for key in sorted(stats):
        label, name = key
        s = stats[key]
        rng = s["max"] - s["min"]
        if rng > 100:
            verdict = "<-- RESPONDS TO PRESS"
        elif rng > 20:
            verdict = "noise"
        else:
            verdict = "frozen"
        print(f"{label:8} {name:4} {s['n']:8d} {s['min']:5d} {s['max']:5d} {rng:6d} {len(s['distinct']):8d}  {verdict}")

if __name__ == "__main__":
    main()
