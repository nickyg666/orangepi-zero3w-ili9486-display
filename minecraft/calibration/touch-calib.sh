#!/bin/bash
# Touch calibration manager for the ILI9486 panel.
#
# Two profiles:
#   hdmi  - touch maps to the full HDMI/DP desktop. Targets are drawn on the
#           HDMI display at 1920x1080; you touch the panel where you see them.
#   fbcp  - touch maps to the panel's mirrored view (WYSIWYG: touch exactly
#           where you see it on the panel). Targets are drawn at panel
#           geometry (480x320) but displayed on HDMI for reading.
#
# xinput_calibrator computes the raw device calibration (libinput Calibration
# Matrix). The Coordinate Transformation Matrix then maps panel space onto the
# target display; both are saved per-profile and applied with xinput.
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
HDMI_CTM="${CONF_DIR}/hdmi.ctm"
HDMI_CAL="${CONF_DIR}/hdmi.cal"
FBCP_CTM="${CONF_DIR}/fbcp.ctm"
FBCP_CAL="${CONF_DIR}/fbcp.cal"

find_display() {
    for d in :2 :0 :1 :3; do
        if DISPLAY="$d" xdpyinfo >/dev/null 2>&1; then echo "$d"; return; fi
    done
    echo ":2"
}

do_calibrate() {
    local profile="$1"
    local ctm_file cal_file geometry
    if [ "$profile" = "hdmi" ]; then
        ctm_file="$HDMI_CTM"; cal_file="$HDMI_CAL"; geometry="1920x1080"
    else
        ctm_file="$FBCP_CTM"; cal_file="$FBCP_CAL"; geometry="480x320"
    fi
    mkdir -p "$CONF_DIR"

    echo "=== $profile calibration ==="
    echo "Targets will appear on the display. Touch each + on the PANEL."
    echo "Press Ctrl-C to abort."
    sleep 1

    OUT=$(DISPLAY="$DISP" xinput_calibrator --device "$DEV" \
        --output-type xorg.conf.d --geometry "$geometry" 2>&1)
    CAL=$(echo "$OUT" | grep -A1 'Option.*CalibrationMatrix' | tail -1 | tr -d '"')

    if [ -z "$CAL" ] || ! echo "$CAL" | grep -qE '^[0-9.-]+ [0-9.-]+ [0-9.-]+'; then
        echo "Calibration failed (did you touch all targets?). Output:"
        echo "$OUT" | tail -6
        return 1
    fi

    # CTM maps the panel's raw space (normalized 0..1) to the display.
    # Panel is 480x320; the panel shows the full display downscaled, so
    # identity (X normalizes by device range) gives WYSIWYG for fbcp and
    # full-desktop control for hdmi. Aspect-fit offset adjustments can be
    # layered on top if the mirror letterboxes.
    CTM="1 0 0 0 1 0 0 0 1"

    echo "$CTM" > "$ctm_file"
    echo "$CAL" > "$cal_file"
    apply_matrix "$CTM" "$CAL"
    echo ""
    echo "Saved $profile profile (CTM: $CTM)"
    echo "Calibration matrix: $CAL"
}

do_apply() {
    local profile="$1"
    local ctm_file cal_file
    if [ "$profile" = "hdmi" ]; then
        ctm_file="$HDMI_CTM"; cal_file="$HDMI_CAL"
    else
        ctm_file="$FBCP_CTM"; cal_file="$FBCP_CAL"
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
