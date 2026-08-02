# Minecraft at 480x320 (native panel res)

To run Minecraft at 480x320 (4x less render work for llvmpipe):
- PrismLauncher instance: /home/orangepi/minecraft/instances/26.1.2/instance.cfg
- Set: OverrideWindow=true, MinecraftWinWidth=480, MinecraftWinHeight=320
- This forces Minecraft to create a 480x320 window (matches the native panel res)

Note: The DRM fb is 960x640 (scale=2), so the game renders 480x320 into the
fb, and the mipi_dbi driver box-filters 960x640->480x320 for the panel.
The game itself does 4x less rendering. Full GPU GL (pvr) works via
surfaceless+zink but the GLX window path fails; see docs/GPU.md.
