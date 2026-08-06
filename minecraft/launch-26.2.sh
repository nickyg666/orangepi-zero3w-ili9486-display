#!/bin/bash
# launch-26.2.sh - Launch Minecraft 26.2 (vanilla) offline on the PowerVR GPU via zink.
# Vanilla 26.2 boots offline (NeoForge 26.1.2 does NOT - needs real MSA login).
# Detached via setsid so it survives the calling shell.
# Usage: sudo -u orangepi ./launch-26.2.sh
AUTH=/run/user/1000/gdm/Xauthority
[ -r "$AUTH" ] || AUTH=/run/user/127/gdm/Xauthority
D=""
for d in :0 :2 :1 :3; do
    if DISPLAY="$d" XAUTHORITY="$AUTH" xdpyinfo >/dev/null 2>&1; then D="$d"; break; fi
done
[ -z "$D" ] && D=:0

cd /home/orangepi/minecraft || exit 1
exec setsid env \
    MESA_LOADER_DRIVER_OVERRIDE=zink \
    MESA_GL_VERSION_OVERRIDE=4.5 \
    MESA_VK_WSI_DEBUG=sw \
    EGL_PLATFORM=x11 \
    ALSOFT_DRIVERS=null \
    DISPLAY="$D" \
    XAUTHORITY="$AUTH" \
    ./PrismLauncher --launch 26.2 --offline orangepi \
    >> /home/orangepi/minecraft/mc-26.2.log 2>&1
