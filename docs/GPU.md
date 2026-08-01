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
