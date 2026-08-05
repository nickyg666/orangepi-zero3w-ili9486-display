#!/bin/bash
# Launch Minecraft on the active X display (HDMI/DP), GPU via zink/PowerVR.
# The SPI panel shows a mirrored copy via spi-mirror.
AUTH=/run/user/1000/gdm/Xauthority
[ -r "$AUTH" ] || AUTH=/run/user/127/gdm/Xauthority
export XAUTHORITY="$AUTH"
D=""
for d in :0 :2 :1 :3; do
    if DISPLAY="$d" xdpyinfo >/dev/null 2>&1; then D="$d"; break; fi
done
[ -z "$D" ] && D=:0
export DISPLAY="$D"
export ALSOFT_CONF=/home/orangepi/.config/openal/alsoft.conf
export ALSOFT_DRIVERS=null
export MESA_LOADER_DRIVER_OVERRIDE=zink
export MESA_GL_VERSION_OVERRIDE=4.5
export MESA_VK_WSI_DEBUG=sw
export EGL_PLATFORM=x11
cd /home/orangepi/minecraft
exec /home/orangepi/minecraft/PrismLauncher "$@"
