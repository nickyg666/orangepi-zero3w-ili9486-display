#!/usr/bin/env python3
"""
calibrate_touch.py - measure the REAL touch surface range and compute a
proper libinput Calibration Matrix + Coordinate Transformation Matrix.

Why this exists: the ADS7846 reports 12-bit raw values but the physical
touchable area does not span the full device range (dead borders, offset,
and the panel is portrait-native). A rotation-only matrix maps assumed
corners to the screen corners, so real corners land inside and the borders
never respond.

This tool:
  1. Reads the actual ABS_X/ABS_Y axis ranges (from evtest) so the matrix
     uses the device's real normalization, not a guess.
  2. Reads raw ABS_X/ABS_Y while you touch the four physical corners of the
     PANEL (hold ~3s each).
  3. Computes the affine mapping from the measured range to the screen
     (scale + offset + rotation), applies it via xinput, and saves profiles.

Usage:
  python3 calibrate_touch.py [hdmi|fbcp]
"""
import subprocess, time, select, os, sys, re

EVDEV = "/dev/input/event1"
DEVICE = "ADS7846 Touchscreen"

def evtest_ranges():
    """Start evtest, read the absinfo header, return {ABS_X:(min,max), ABS_Y:(min,max)}."""
    p = subprocess.Popen(["evtest", EVDEV], stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL, text=True)
    os.set_blocking(p.stdout.fileno(), False)
    buf = ""
    ranges = {}
    t0 = time.time()
    while len(ranges) < 2 and time.time() - t0 < 5:
        r, _, _ = select.select([p.stdout], [], [], 0.2)
        if not r:
            continue
        buf += os.read(p.stdout.fileno(), 8192).decode("utf-8", "ignore")
        # lines like: "      Value      0, Min      0, Max    4095"
        m = re.findall(r'ABS_([XY])\s+.*?Value\s+(\d+),\s+Min\s+(\d+),\s+Max\s+(\d+)',
                       buf, re.DOTALL)
        for axis, v, mn, mx in m:
            ranges["ABS_" + axis] = (int(mn), int(mx))
    return p, ranges

def raw_sample(p, seconds=3.5):
    """Return average (x, y) raw device values while the user holds a touch."""
    buf = ""
    x = y = None
    sx = sy = n = 0
    t0 = time.time()
    while time.time() - t0 < seconds:
        r, _, _ = select.select([p.stdout], [], [], 0.2)
        if not r:
            continue
        buf += os.read(p.stdout.fileno(), 8192).decode("utf-8", "ignore")
        while "\n" in buf:
            line, buf = buf.split("\n", 1)
            if "code 0 (ABS_X), value" in line:
                x = int(line.split("value")[-1])
            elif "code 1 (ABS_Y), value" in line and x is not None:
                y = int(line.split("value")[-1])
                sx += x
                sy += y
                n += 1
    return (sx / n, sy / n) if n else (None, None)

def main():
    profile = sys.argv[1] if len(sys.argv) > 1 else "hdmi"

    p, ranges = evtest_ranges()
    if not ranges:
        print("Could not read axis ranges from evtest!", flush=True)
        return 1
    xmin_dev, xmax_dev = ranges["ABS_X"]
    ymin_dev, ymax_dev = ranges["ABS_Y"]
    print(f"Device axis ranges: X=[{xmin_dev},{xmax_dev}] Y=[{ymin_dev},{ymax_dev}]",
          flush=True)

    # Physical corner order: TOP-LEFT, TOP-RIGHT, BOTTOM-RIGHT, BOTTOM-LEFT
    # of the PANEL as displayed (landscape).
    corners = []
    for name in ["TOP-LEFT", "TOP-RIGHT", "BOTTOM-RIGHT", "BOTTOM-LEFT"]:
        print(f"\n>>> Touch the {name} corner of the PANEL, hold ~3s ...",
              flush=True)
        x, y = raw_sample(p)
        if x is None:
            print("    No samples! Retry.", flush=True)
            corners = None
            break
        print(f"    measured raw X={x:.0f} Y={y:.0f}", flush=True)
        corners.append((x, y))
        print("    lift, wait 2s", flush=True)
        time.sleep(2)

    if not corners:
        return 1

    xmin_raw = min(c[0] for c in corners)
    xmax_raw = max(c[0] for c in corners)
    ymin_raw = min(c[1] for c in corners)
    ymax_raw = max(c[1] for c in corners)

    if xmax_raw <= xmin_raw or ymax_raw <= ymin_raw:
        print("Bad measurements - corners must differ.", flush=True)
        return 1

    # The panel is portrait-native.  libinput normalizes device coords to
    # 0..1 by dividing by the device axis max BEFORE applying the matrix.
    # To map the measured physical range onto the full screen (landscape):
    #   screen_x = (raw_y - ymin_raw) / (ymax_raw - ymin_raw)
    #   screen_y = (xmax_raw - raw_x) / (xmax_raw - xmin_raw)
    # In normalized device coords (n = raw / dev_max):
    #   screen_x = n_y * (dev_ymax/(ymax-ymin)) - ymin/(ymax-ymin)
    #   screen_y = -n_x * (dev_xmax/(xmax-xmin)) + xmax/(xmax-xmin)
    sx_scale = (ymax_dev - ymin_dev + 1) / (ymax_raw - ymin_raw)
    sy_scale = (xmax_dev - xmin_dev + 1) / (xmax_raw - xmin_raw)
    cal = [0.0, sx_scale, -ymin_raw / (ymax_raw - ymin_raw),
           -sy_scale, 0.0, xmax_raw / (xmax_raw - xmin_raw),
           0.0, 0.0, 1.0]
    cal_s = " ".join(f"{v:.6f}" for v in cal)

    ctm = "1 0 0 0 1 0 0 0 1"

    print(f"\n=== Computed calibration for profile '{profile}' ===")
    print(f"Raw X range: [{xmin_raw:.0f}, {xmax_raw:.0f}]")
    print(f"Raw Y range: [{ymin_raw:.0f}, {ymax_raw:.0f}]")
    print(f"libinput Calibration Matrix: {cal_s}")
    print(f"Coordinate Transformation Matrix: {ctm}")

    auth = "/run/user/1000/gdm/Xauthority"
    if not os.access(auth, os.R_OK):
        auth = "/run/user/127/gdm/Xauthority"
    disp = None
    for d in [":0", ":2", ":1", ":3"]:
        r = subprocess.run(["xdpyinfo"], env={**os.environ, "DISPLAY": d,
                                             "XAUTHORITY": auth},
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if r.returncode == 0:
            disp = d
            break
    if not disp:
        print("No X display found!", flush=True)
        return 1

    env = {**os.environ, "DISPLAY": disp, "XAUTHORITY": auth}

    def xset(prop, val):
        subprocess.run(["xinput", "set-prop", DEVICE, prop] + val.split(),
                       env=env, check=True)

    xset("Coordinate Transformation Matrix", ctm)
    xset("libinput Calibration Matrix", cal_s)
    print(f"Applied to '{DEVICE}' on {disp}.")

    conf = os.path.expanduser("~/.config/touch-calib")
    os.makedirs(conf, exist_ok=True)
    for pr in ["hdmi", "fbcp"]:
        with open(f"{conf}/{pr}.ctm", "w") as f:
            f.write(ctm + "\n")
        with open(f"{conf}/{pr}.cal", "w") as f:
            f.write(cal_s + "\n")
    print("Profiles saved to ~/.config/touch-calib/")
    return 0

if __name__ == "__main__":
    sys.exit(main())
