#!/bin/bash
# Mirror the primary display onto the SPI panel.
# - Find the real gdm X display (the one with HDMI/DP on card0).
# - If an external display is connected: mirror that X display.
# - Else (headless): mirror Xvfb :1 (720p dummy) so GPU apps still show.
AUTH=/run/user/1000/gdm/Xauthority
[ -r $AUTH ] || AUTH=/run/user/127/gdm/Xauthority

find_display() {
    for d in :2 :0 :1 :3; do
        if DISPLAY=$d XAUTHORITY=$AUTH xdpyinfo >/dev/null 2>&1; then
            echo $d
            return
        fi
    done
}

while true; do
    ext=0
    for c in /sys/class/drm/card0-*/status; do
        [ "$(cat $c 2>/dev/null)" = "connected" ] && ext=1
    done
    if [ $ext -eq 1 ]; then
        D=$(find_display)
        [ -z "$D" ] && D=:2
        DISPLAY=$D XAUTHORITY=$AUTH /home/orangepi/spi-mirror "$D"
    else
        DISPLAY=:1 /home/orangepi/spi-mirror :1
    fi
    echo "spi-mirror-select: restarting (ext=$ext)"
    sleep 3
done
