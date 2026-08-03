#!/bin/bash
# Detached Minecraft launcher - survives parent shell death
exec setsid su - orangepi -c "/home/orangepi/minecraft/game-run.sh -l 26.1.2 > /home/orangepi/minecraft/game-run.log 2>&1" < /dev/null > /dev/null 2>&1 &
disown
exit 0
