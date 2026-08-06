#!/bin/bash
# Detached Minecraft launcher on the active display.
# Usage: launch-detached.sh [instance]   (default: 26.1.2)
INST="${1:-26.1.2}"
exec setsid su - orangepi -c "/home/orangepi/minecraft/game-run.sh -l $INST > /home/orangepi/minecraft/game-run.log 2>&1" < /dev/null > /dev/null 2>&1 &
disown
exit 0
