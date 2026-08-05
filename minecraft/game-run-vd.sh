#!/bin/bash
# Run Minecraft on the virtual 1920x1080 display (:1) with GPU (zink).
# The SPI panel shows a mirrored copy via spi-mirror.
export DISPLAY=:1
unset XAUTHORITY
export ALSOFT_CONF=/home/orangepi/.config/openal/alsoft.conf
export ALSOFT_DRIVERS=null
export MESA_LOADER_DRIVER_OVERRIDE=zink
export MESA_GL_VERSION_OVERRIDE=4.5
export MESA_VK_WSI_DEBUG=sw,linear
export EGL_PLATFORM=x11
cd /home/orangepi/minecraft
exec /home/orangepi/minecraft/PrismLauncher "$@"
