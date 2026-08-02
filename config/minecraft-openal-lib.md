# Minecraft OpenAL crash - use system libopenal
The LWJGL-bundled libopenal crashes (blr x1 with NULL callback) during audio
device setup under Java 25. Fix: force LWJGL to use the working system
libopenal via instance JvmArgs:
  JvmArgs=-Dorg.lwjgl.openal.libname=/usr/lib/aarch64-linux-gnu/libopenal.so.1
Also keep: OpenAL null driver (~/.config/openal/alsoft.conf) since there's
no audio output hardware, and Java 25 (required by MC 26.1).
