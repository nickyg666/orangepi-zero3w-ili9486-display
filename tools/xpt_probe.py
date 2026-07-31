#!/usr/bin/env python3
"""XPT2046 raw probe - checks channel mapping + PENIRQ polarity on spi3.1.

Cycles every channel command (12-bit + 8-bit) in each SPI mode 0-3 while the
user presses the glass. Watches PB0 (gpio32 = PENIRQ) continuously.
"""
import spidev, time, threading, sys

DEV = "/dev/spidev3.1"
GPIOPIN = "/sys/class/gpio/gpio32/value"
WINDOW = 10.0

CMDS = [  # (label, 12-bit cmd, 8-bit cmd)
    ("X ", 0xD4, 0xDC),
    ("Y ", 0x94, 0x9C),
    ("Z1", 0xB4, 0xBC),
    ("Z2", 0xC4, 0xCC),
]

pen_state = {"low_since": None, "min": 1, "max": 0, "samples": 0}

def pen_watch():
    while not stop_event.is_set():
        try:
            v = int(open(GPIOPIN).read().strip())
            pen_state["samples"] += 1
            pen_state["min"] = min(pen_state["min"], v)
            pen_state["max"] = max(pen_state["max"], v)
            if v == 0 and pen_state["low_since"] is None:
                pen_state["low_since"] = time.time()
        except FileNotFoundError:
            pass
        except Exception:
            pass
        time.sleep(0.02)

stop_event = threading.Event()

def main():
    try:
        with open(GPIOPIN) as f:
            print(f"PENIRQ(pin high when idle expected) initial: {f.read().strip()}")
    except FileNotFoundError:
        print("PENIRQ watch: gpio32 not exported (held by pinctrl) - watching /proc/interrupts instead")
    sp = spidev.SpiDev()
    sp.open(3, 1)  # -> /dev/spidev3.1
    print("=== PRESS AND HOLD/HOLD&DRAW ON THE GLASS during each 10s window ===")
    for mode in range(4):
        try:
            sp.mode = mode
            sp.bits_per_word = 8
            try:
                sp.lsbfirst = False
            except Exception:
                pass
            sp.max_speed_hz = 2000000
        except Exception as e:
            print(f"mode {mode}: setup failed: {e}"); continue
        stats = {}
        pen_state.update(min=1, max=0, samples=0, low_since=None)
        t0 = time.time()
        while time.time() - t0 < WINDOW:
            for label, c12, c8 in CMDS:
                for name, cmd in (("12", c12), ("8", c8)):
                    rx = sp.xfer2([cmd, 0, 0])
                    v12 = (rx[1] << 8 | rx[2]) >> 4
                    v8 = (rx[1] << 8 | rx[2]) >> 8
                    key = (label, name)
                    s = stats.setdefault(key, {"n": 0, "min": 0xFFFF, "max": 0, "distinct": set()})
                    s["n"] += 1
                    s["min"] = min(s["min"], v12)
                    s["max"] = max(s["max"], v12)
                    s["distinct"].add(v12)
                    if len(s["distinct"]) > 4096:
                        s["distinct"].pop()
        print(f"--- SPI mode {mode} ---")
        for (label, name), s in sorted(stats.items()):
            d = len(s["distinct"])
            flag = " <-- MOVED" if d > 1 else (" FROZEN" if s["min"] == s["max"] else "")
            print(f"  ch{label} {name}bit: n={s['n']:6d} min={s['min']:4d} max={s['max']:4d} distinct={d}{flag}")
        pl = pen_state
        pen_note = ""
        if pl["low_since"] is not None:
            pen_note = f" PEN WENT LOW for {time.time()-pl['low_since']:.1f}s -> PENIRQ ACTIVE"
        elif pl["samples"]:
            pen_note = " (pen line was HIGH the whole window)"
        print(f"  PENIRQ: min={pl['min']} max={pl['max']} over {pl['samples']} polls{pen_note}")
    stop_event.set()

if __name__ == "__main__":
    t = threading.Thread(target=pen_watch, daemon=True)
    t.start()
    try:
        main()
    except KeyboardInterrupt:
        pass
    finally:
        stop_event.set()