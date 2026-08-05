#!/bin/bash
# Mirror the primary display onto the SPI panel.
# - If an external display (HDMI/DP on card0) is connected: mirror :0 (real X).
# - Else (headless): mirror :1 (Xvfb 720p dummy) so GPU apps still show.
while true; do
    ext=0
    for c in /sys/class/drm/card0-*/status; do
        [ "$(cat $c 2>/dev/null)" = "connected" ] && ext=1
    done
    if [ $ext -eq 1 ]; then
        export DISPLAY=:0 XAUTHORITY=/run/user/1000/gdm/Xauthority
        /home/orangepi/spi-mirror :0
    else
        export DISPLAY=:1
        /home/orangepi/spi-mirror :1
    fi
    echo "spi-mirror-select: restarting ($ext)"
    sleep 3
done
