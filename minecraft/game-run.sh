#!/bin/bash
# Launch Minecraft on the real X display (:0, HDMI/DP + SPI). zink renders
# on the PowerVR GPU; the SPI panel shows a copy via spi-mirror.
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
