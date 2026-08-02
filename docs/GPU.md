# GPU Status (PowerVR)

## Hardware
- GPU: PowerVR B-Series BXM-4-64 MC1 (Rogue)
- Kernel driver: pvrsrvkm.ko (DKMS, v24.2.6603887) - creates /dev/dri/card2
- DRM pvr driver initialized on minor 2

## Userspace drivers (present)
- Vulkan: libVK_IMG.so (v24.2.6603887) at /usr/lib/, ICD /usr/share/vulkan/icd.d/img_icd.json
  - VERIFIED WORKING: vulkaninfo reports PowerVR BXM-4-64 MC1, API 1.3.277
- OpenGL/EGL: pvr_dri.so + vendor mesa WSI (libpvr_mesa_wsi.so) in /usr/local/lib/
- Mesa pvr_dri.so at /usr/lib/aarch64-linux-gnu/dri/pvr_dri.so (not auto-selected)

## Current GL situation
- X session uses fbdev (fb1) -> GLX falls back to llvmpipe (software)
- GPU is NOT used for desktop compositing yet

## To use the GPU
1. Configure Mutter/gnome-shell to use PowerVR EGL (vendor libs in /usr/local/lib)
2. Or use Vulkan directly for compositing/downscale
3. GPU could accelerate: gnome-shell compositing, box-filter downscale (960x640->480x320)

## GL/EGL integration findings
- Vendor GL stack (/usr/local/lib, Mesa 24.0.1) initializes but only via zink (Vulkan-over-GL)
- zink_dri.so missing from /usr/local/lib/dri -> falls back to softpipe (software)
- System Mesa (23.2.1) + system zink_dri.so exist but version-mismatch with vendor EGL
- zink over X11 GLX fails DRI2 auth -> GL_RENDERER null
- CONCLUSION: GL desktop compositing via GPU not feasible without vendor fix.
  Raw Vulkan WORKS (PowerVR BXM-4-64, API 1.3.277) - usable for custom rendering
  (e.g. GPU-accelerated video player -> downscale -> SPI framebuffer).

## Updated findings (post-gpu-vpu install)
- Vendor /usr/local/lib/dri/pvr_dri.so HAS __driDriverGetExtensions_pvr
- But vendor EGL (Mesa 24.0.1) falls back to softpipe even with
  MESA_LOADER_DRIVER_OVERRIDE=pvr - pvr DRI driver fails to initialize
  (DRI2 auth under X11/fbdev)
- Vulkan works fully (libVK_IMG, PowerVR BXM-4-64 MC1, API 1.3.277)
- CONCLUSION: desktop GL compositing via PowerVR not working in this
  X11+fbdev config; vendor EGL is Mesa 24.0.1 but pvr_dri silently fails.
  Use Vulkan for custom GPU rendering. Temps OK at 61C idle (llvmpipe
  only burns CPU when actively compositing).

## BREAKTHROUGH: PowerVR GL WORKS with the right env
Command that yields GPU GL rendering:
  LD_LIBRARY_PATH=/usr/local/lib \
  LIBGL_DRIVERS_PATH=/usr/local/lib/dri \
  MESA_LOADER_DRIVER_OVERRIDE=pvr \
  EGL_PLATFORM=surfaceless \
  <gl app>
Result:
  GL_VENDOR: Imagination Technologies
  GL_RENDERER: PowerVR B-Series BXM-4-64
The vendor Mesa stack (/usr/local/lib, from Incipiens GPU-VPU) DOES work.
Earlier failures were because EGL_PLATFORM defaulted to x11 (broken) and
the pvr driver wasn't forced. surfaceless + override=pvr is the key.
Next: wire this env into gnome-shell/Mutter for GPU compositing, and
into the Wayland session (which previously crashed on EGL init).

## CAUTION: pvr GL works standalone but breaks mutter
- eglgl test: pvr works with EGL_PLATFORM=surfaceless (PowerVR BXM-4-64)
- BUT setting MESA_LOADER_DRIVER_OVERRIDE=pvr globally (Xsession.d) makes
  gnome-shell/mutter CRASH (g_str_has_prefix NULL -> signal 11) because
  mutter uses the x11 EGL platform, which the vendor stack can't handle.
- REVERTED: removed Xsession.d/99-pvr-gpu and /etc/environment overrides.
- The Wayland crash is the SAME x11-EGL/card0 issue. Standalone pvr GL
  (surfaceless) works; integrating into mutter needs the x11/EGL platform
  working (vendor stack gap) or mutter patched.
- Current state: X11 desktop stable, llvmpipe GL (CPU). GPU usable via
  standalone Vulkan/GL apps with surfaceless+override=pvr.

## PROVEN: GPU -> SPI framebuffer pipeline works
test-utils/gpu_fbo.c renders a GL gradient on the PowerVR GPU (EGL
surfaceless + pvr override), reads it back from an FBO, converts to RGB565,
and writes to /dev/fb0 (which the mipi_dbi driver flushes to SPI).
Output: "GPU GL: PowerVR B-Series BXM-4-64", readback gradient px[0]=160,142,71.
This is the path for GPU-accelerated apps (video players, games) that
bypass mutter's broken EGL: render offscreen on GPU -> write fb -> SPI.
Note: wrote a 480x320 frame into the 960x640 fb (top-left quadrant);
a real app should render at 960x640 and box-filter, or the driver should
scale. This proves the hardware path; app integration is the next step.

## Minecraft/GLX GPU paths (tested)
1. Native pvr GL (MESA_LOADER_DRIVER_OVERRIDE=pvr): works SURFACELESS
   (offscreen FBO readback -> fb proven). FAILS GLX window: DRI2 auth /
   X_GLXCreateNewContext BadValue on the fbdev X server.
2. zink (GL-over-Vulkan, MESA_LOADER_DRIVER_OVERRIDE=zink): reaches PowerVR
   (glxinfo shows "zink Vulkan 1.3 PowerVR BXM-4-64") but fails because the
   GPU lacks fillModeNonSolid (zink base requirement) -> swapchain error.
   Also "some incorrect rendering" warning.
=> No GLX window path to the GPU on this setup. GPU usable offscreen only
   (surfaceless + FBO readback -> write fb). Minecraft on llvmpipe; use
   480x320 window (instance.cfg OverrideWindow) for 4x faster rendering.

## GLAMOR/modesetting experiment (tested)
- modesetting driver on card1 (SPI, ili9486): sees "Unknown19-1" output, probed
  modes OK, but glamor initialization FAILS on card1 (mipi_dbi has no GPU).
- modesetting driver on card0/GPU: "glamor X acceleration enabled on PowerVR
  B-Series BXM-4-64" - GLAMOR DOES init on the PowerVR via modesetting!
- But screen binds to card1 (display), which can't glamor -> "no screens found".
- CONCLUSION: GPU (card2) and display (card1) are SEPARATE DRM devices. To use
  GPU glamor for the SPI display, the GPU would need to render INTO card1's
  framebuffer - complex cross-device setup not feasible here.
- zink needs DRI3 for presentation; fbdev X has no DRI3 -> GPU GL can't present
  to windows on this display. pvr GL works offscreen only (surfaceless).

## Chromium WebGL (ANGLE-on-Vulkan) - WORKS
- chromium with --use-gl=angle --use-angle=vulkan --enable-features=Vulkan
  - GPU process opens /dev/dri/renderD128 (pvr) - confirmed via /proc fds.
- This bypasses GLX/DRI3 entirely (Chromium uses its own Vulkan WSI), so it
  can present GPU-rendered WebGL to the X window even on fbdev.
- This is the most viable GPU-windowed path on this setup. Desktop shortcuts:
  ~/Desktop/chromium-webgl-test.desktop (aquarium), chromium-gpu-info.desktop.
