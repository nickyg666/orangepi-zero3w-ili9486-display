#!/bin/bash
# PrismLauncher wrapper - launch on the LCD session
export DISPLAY=:0
export XAUTHORITY=/run/user/1000/gdm/Xauthority
cd /home/orangepi/minecraft
exec /home/orangepi/minecraft/PrismLauncher "$@"
