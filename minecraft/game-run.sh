#!/bin/bash
# Launch Minecraft (NeoForge 26.1.2) via PrismLauncher with zink/PowerVR GPU accel
# Fixes: OpenAL null driver (audio enum segfault), native 480x320 display
export DISPLAY=:0
export XAUTHORITY=/run/user/1000/gdm/Xauthority
export ALSOFT_CONF=/home/orangepi/.config/openal/alsoft.conf
export ALSOFT_DRIVERS=null
export MESA_LOADER_DRIVER_OVERRIDE=zink
export MESA_GL_VERSION_OVERRIDE=4.5
export MESA_VK_WSI_DEBUG=sw
export EGL_PLATFORM=x11
cd /home/orangepi/minecraft
exec /home/orangepi/minecraft/PrismLauncher "$@"
