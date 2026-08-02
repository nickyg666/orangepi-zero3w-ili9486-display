# Minecraft OpenAL crash fix (exit code 6)
Crash: JVM SIGSEGV in libopenal.so (alcGetString) at launch.
Cause: OpenAL ALSA backend fails on the allwinnerhdmi-only audio setup.
Fix: /home/orangepi/.config/openal/alsoft.conf with `drivers = null`
     (+ ALSOFT_CONF exported in prism-launch.sh wrapper).
Also: Minecraft options.txt overrideWidth=480 overrideHeight=320 (native
      panel res) to fix the fullscreen resolution.
