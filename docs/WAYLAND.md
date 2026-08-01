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
