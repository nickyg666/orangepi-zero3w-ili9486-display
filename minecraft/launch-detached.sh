#!/bin/bash
# Detached Minecraft launcher on the real display (:0)
exec setsid su - orangepi -c "/home/orangepi/minecraft/game-run.sh -l 26.1.2 > /home/orangepi/minecraft/game-run.log 2>&1" < /dev/null > /dev/null 2>&1 &
disown
exit 0
