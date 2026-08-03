#!/bin/bash
# PrismLauncher wrapper - LCD session with GPU (zink/PowerVR) acceleration
export DISPLAY=:0
export XAUTHORITY=/run/user/1000/gdm/Xauthority
export ALSOFT_CONF=/home/orangepi/.config/openal/alsoft.conf
# OpenAL segfaults enumerating audio devices with no audio server running - force null driver
export ALSOFT_DRIVERS=null
# GPU: zink (GL-on-Vulkan) on the PowerVR - desktop GL 4.5 core
export MESA_LOADER_DRIVER_OVERRIDE=zink
export MESA_GL_VERSION_OVERRIDE=4.5
# No DRI3 on this SPI-panel X server; force Mesa's software-present fallback so zink can present windows
export MESA_VK_WSI_DEBUG=sw
export EGL_PLATFORM=x11
cd /home/orangepi/minecraft
exec /home/orangepi/minecraft/PrismLauncher "$@"
