#!/bin/bash
# Touch calibration manager for the ILI9486 panel.
#
# Two profiles:
#   hdmi  - touch maps to the full HDMI/DP desktop
#   fbcp  - touch maps to the panel mirror (WYSIWYG)
#
# Calibration: fullscreen crosshair GUI (calibrate-gui) that waits
# indefinitely for touches at each corner, measures the REAL physical
# touch range, computes the affine matrix, applies it, and saves it.
#
# Usage:
#   touch-calib.sh cal hdmi|fbcp   - run interactive calibration, save profile
#   touch-calib.sh apply hdmi|fbcp - apply a saved profile
#   touch-calib.sh show            - show current matrices

AUTH=/run/user/1000/gdm/Xauthority
[ -r "$AUTH" ] || AUTH=/run/user/127/gdm/Xauthority
export XAUTHORITY="$AUTH"
DEV="ADS7846 Touchscreen"
CONF_DIR=/home/orangepi/.config/touch-calib

find_display() {
    for d in :0 :2 :1 :3; do
        if DISPLAY="$d" xdpyinfo >/dev/null 2>&1; then echo "$d"; return; fi
    done
    echo ":0"
}

do_calibrate() {
    local profile="$1"
    if [ "$profile" != "hdmi" ] && [ "$profile" != "fbcp" ]; then
        echo "usage: touch-calib.sh cal hdmi|fbcp"
        return 1
    fi
    mkdir -p "$CONF_DIR"
    echo "=== $profile calibration ==="
    echo "A fullscreen crosshair will appear. Touch the PANEL at each"
    echo "crosshair and HOLD - wait for the next one. No time limit."
    sleep 2
    /home/orangepi/calibrate-gui "$profile" "$DISP"
    echo "Calibration done."
}

do_apply() {
    local profile="$1"
    local ctm_file cal_file
    if [ "$profile" = "hdmi" ]; then
        ctm_file="$CONF_DIR/hdmi.ctm"; cal_file="$CONF_DIR/hdmi.cal"
    else
        ctm_file="$CONF_DIR/fbcp.ctm"; cal_file="$CONF_DIR/fbcp.cal"
    fi
    if [ ! -f "$ctm_file" ]; then
        echo "No saved $profile profile - run: touch-calib.sh cal $profile"
        return 1
    fi
    apply_matrix "$(cat "$ctm_file")" "$(cat "$cal_file")"
    echo "Applied $profile profile."
}

apply_matrix() {
    DISPLAY="$DISP" xinput set-prop "$DEV" "Coordinate Transformation Matrix" $1 2>/dev/null
    if [ -n "$2" ]; then
        DISPLAY="$DISP" xinput set-prop "$DEV" "libinput Calibration Matrix" $2 2>/dev/null
    fi
}

do_show() {
    DISPLAY="$DISP" xinput list-props "$DEV" 2>/dev/null | grep -A1 -iE "Coordinate Transformation Matrix|libinput Calibration Matrix \(" | head -6
}

DISP=$(find_display)
export DISPLAY="$DISP"

case "$1" in
    cal)   [ -n "$2" ] && do_calibrate "$2" || echo "usage: touch-calib.sh cal hdmi|fbcp" ;;
    apply) [ -n "$2" ] && do_apply "$2" || echo "usage: touch-calib.sh apply hdmi|fbcp" ;;
    show)  do_show ;;
    *) echo "usage: touch-calib.sh cal|apply|show [hdmi|fbcp]" ;;
esac
