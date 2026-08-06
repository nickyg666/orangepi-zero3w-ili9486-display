#!/usr/bin/env python3
"""
calibrate_touch.py - measure the REAL touch surface range and compute a
proper Coordinate Transformation Matrix + libinput Calibration Matrix.

Why this exists: the ADS7846 reports 0-65535 but the physical touchable
area does not span that full range (dead borders, offset, and the panel is
portrait-native). A rotation-only matrix maps the assumed corners to the
screen corners; the actual corners then land inside the screen, so the
edges/borders never respond.

This tool:
  1. Reads raw ABS_X/ABS_Y from /dev/input/event1 while you touch the four
     physical corners of the PANEL (hold ~3s each).
  2. Computes the affine mapping from the measured range to the screen.
  3. Applies it via xinput and writes the profiles for touch-calib.sh.

Usage:
  python3 calibrate_touch.py [hdmi|fbcp]
"""
import subprocess, time, select, os, sys, re

EVDEV = "/dev/input/event1"
DEVICE = "ADS7846 Touchscreen"
SCREENS = {"hdmi": (1920, 1080), "fbcp": (480, 320)}

def raw_sample(seconds=3.5):
    """Return average (x, y) raw device values while the user holds a touch."""
    p = subprocess.Popen(["evtest", EVDEV], stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL, text=True)
    os.set_blocking(p.stdout.fileno(), False)
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
    p.terminate()
    return (sx / n, sy / n) if n else (None, None)

def main():
    profile = sys.argv[1] if len(sys.argv) > 1 else "hdmi"
    sw, sh = SCREENS.get(profile, SCREENS["hdmi"])

    # Physical corner order: TOP-LEFT, TOP-RIGHT, BOTTOM-RIGHT, BOTTOM-LEFT
    # of the PANEL as displayed (landscape).
    corners = []
    for name in ["TOP-LEFT", "TOP-RIGHT", "BOTTOM-RIGHT", "BOTTOM-LEFT"]:
        print(f"\n>>> Touch the {name} corner of the PANEL, hold ~3s ...",
              flush=True)
        x, y = raw_sample()
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

    (tlx, tly), (trx, _), (brx, bry), (blx, _) = corners

    # The panel is portrait-native (320 wide x 480 tall in raw device space)
    # rotated to landscape.  From the corner measurements:
    #   device X spans [tly-ish ... bry] (the long axis) after rotation,
    #   device Y spans [tlx-ish ... trx].
    # We build a full affine calibration directly:
    #
    # For a WYSIWYG mapping we need: touch at panel pixel (px,py) ->
    # screen pixel (sx,sy).  The measured corners give the raw device range
    # of the touchable area; map that range onto the full screen.
    #
    # Raw device space (what the ADS7846 emits):
    #   X_raw in [xmin, xmax], Y_raw in [ymin, ymax]  (portrait)
    # After the libinput Calibration Matrix (which we are computing) the
    # device is reoriented to landscape: long axis becomes X.

    # Portrait raw axes: X_raw is the SHORT axis (0..~4096), Y_raw is the
    # LONG axis. Compute the full range from all four corner samples.
    xmin_raw = min(c[0] for c in corners)
    xmax_raw = max(c[0] for c in corners)
    ymax_raw = max(c[1] for c in corners)
    ymin_raw = min(c[1] for c in corners)

    if xmax_raw <= xmin_raw or ymax_raw <= ymin_raw:
        print("Bad measurements - corners must differ.", flush=True)
        return 1

    # libinput works in NORMALIZED coords. The driver exposes 0..65535.
    # Normalized device coord = raw/65535. We want the measured physical
    # range [xmin_raw, xmax_raw] x [ymin_raw, ymax_raw] to map onto the
    # full screen [0,1] x [0,1] AFTER rotation.
    #
    # Calibration Matrix M maps raw(x,y) -> oriented(x',y') then screen:
    #   screen_x = (raw_y - ymin_raw) / (ymax_raw - ymin_raw)
    #   screen_y = (raw_x - xmin_raw) / (xmax_raw - xmin_raw)
    # (landscape: long axis raw_y -> screen x, short axis raw_x -> screen y)
    #
    # In matrix form (rows: screen_x, screen_y):
    #   [ 0, 1/(ymax-ymin), -ymin/(ymax-ymin) ]
    #   [ -1/(xmax-xmin), 0,  xmax/(xmax-xmin) ]
    a = 1.0 / (ymax_raw - ymin_raw)
    b = 1.0 / (xmax_raw - xmin_raw)
    cal = [0.0, a, -ymin_raw * a,
           -b, 0.0, xmax_raw * b,
           0.0, 0.0, 1.0]

    cal_s = " ".join(f"{v:.6f}" for v in cal)

    # Coordinate Transformation Matrix: identity (normalized 1:1) - the
    # calibration already maps the physical range onto the full screen.
    ctm = "1 0 0 0 1 0 0 0 1"

    print(f"\n=== Computed calibration for profile '{profile}' ===")
    print(f"Raw X range: [{xmin_raw:.0f}, {xmax_raw:.0f}]")
    print(f"Raw Y range: [{ymin_raw:.0f}, {ymax_raw:.0f}]")
    print(f"libinput Calibration Matrix: {cal_s}")
    print(f"Coordinate Transformation Matrix: {ctm}")

    # Find display + auth
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

    # Save profiles for touch-calib.sh
    conf = os.path.expanduser("~/.config/touch-calib")
    os.makedirs(conf, exist_ok=True)
    for p in ["hdmi", "fbcp"]:
        with open(f"{conf}/{p}.ctm", "w") as f:
            f.write(ctm + "\n")
        with open(f"{conf}/{p}.cal", "w") as f:
            f.write(cal_s + "\n")
    print("Profiles saved to ~/.config/touch-calib/")
    return 0

if __name__ == "__main__":
    sys.exit(main())
