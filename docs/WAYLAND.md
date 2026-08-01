# Wayland on the ILI9486 SPI display

## Status: WORKING at greeter level, user session forced to X11 by gdm

## What works
- gnome-shell (mutter 42.9) STARTS as a Wayland display server on the SPI
  display (card1). Verified: "Running GNOME Shell as a Wayland display server".
- The gdm greeter runs as Wayland when WaylandEnable=true.

## The blocker
- With autologin, the USER session always launches via gdm-x-session (X11),
  even though the greeter ran Wayland. gdm-session-worker only checks the
  session file location (/usr/share/wayland-sessions/); the display-server
  selection for the user session falls back to X11.
- gdm 42.0. Env vars seen: XDG_DATA_DIRS=/usr/share/ubuntu-wayland (session
  is recognized as wayland) but gdm-x-session is spawned.

## Tests done
- Manual: `gnome-shell --wayland` with X stopped -> EBUSY (X held DRM).
- Manual: gdm-wayland-session wrapper -> started, CRTC committed.
- gdm restart with WaylandEnable=true -> greeter ran Wayland (22:05:44),
  user session went X11 3s later (22:05:47).

## Next steps (not done)
- Investigate gdm-session.c display-server selection (needs gdm source).
- Try disabling autologin: user session may inherit greeter's Wayland.
- Try a custom Wayland session that gdm won't force to X11.

## ROOT CAUSE FOUND (22:11)
gnome-shell Wayland compositor SEGFAULTS (signal 11) during init:
  - "Added device '/dev/dri/card2' (pvr) using non-atomic mode setting"
  - "Added device '/dev/dri/card1' (ili9486) using atomic mode setting"
  - "Added device '/dev/dri/card0' (sunxi-drm) using atomic mode setting"
  - "[drm] sunxi-hdmi: drm hdmi detect: disconnect"
  - "g_str_has_prefix: assertion 'str != NULL' failed" (x3)
  - "Failed to get string: No error has been recorded."
  - "Application 'org.gnome.Shell.desktop' killed by signal 11"

The crash is triggered by the sunxi-drm HDMI (card0) DISCONNECT event:
mutter reads a NULL mode/string from the disconnected HDMI connector and
g_str_has_prefix() asserts -> segfault.

## Fix directions (not yet applied)
1. Make mutter ignore card0 (HDMI): no obvious env/gsetting; could try
   removing master-of-seat udev tag from card0.
2. Keep HDMI "connected" with a dummy mode so mutter doesn't hit the
   disconnect path (driver-side).
3. Patch mutter's g_str_has_prefix NULL guard (needs mutter source).
4. Use X11 (current stable) and add GPU via a different path.

## Status: BLOCKED on mutter segfault
No mutter env/gsetting exists to exclude a DRM device. udev TAG- can't
strip seat tags (71-seat.rules re-adds them). Fix needs a mutter patch
or sunxi-drm driver workaround for the HDMI disconnect path.
X11 remains the stable display path.
