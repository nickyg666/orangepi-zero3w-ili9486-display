# Workspace Rules

## Display / KMS Policy (CRITICAL — read before any display work)

This board (Orange Pi Zero 3W, sun60iw2) has three possible displays:

- **HDMI-A-1** (card0, sunxi-drm + sunxi-hdmi)
- **USB-C** (connected via `twi@7084000/husb311@22/connector` — a second DP/display path)
- **ILI9486 SPI panel** (card1, custom `ili9486` + `drm_mipi_dbi` modules, DRM minor 1, `fb0: ili9486drmfb`)

**Mandatory initialization order:**

1. **Always initialize HDMI and USB-C displays FIRST.** They are the primary displays
   and must be brought up before the SPI panel. Never add `no-sunxi-drm` or anything that
   tears down `sunxi-drm`/`sunxi-hdmi`, and never suppress the HDMI connector.
2. **Duplicate/mirror the first-initialized display onto the SPI panel.** The ili9486
   SPI display must always show a copy of whichever external display initialized first
   (HDMI or USB-C). Do not drive the SPI panel as an independent/orphan screen.
3. Never edit `/boot/orangepiEnv.txt` `overlays=`/`extraargs=` to disable sunxi-drm or
   force HDMI off. If you think you need to, you are wrong — re-read this file.

### Boot config invariants (must remain true)

- `overlays=ili9486` — the ili9486.dtbo must exist at
  `/boot/dtb-6.6.98-sun60iw2/allwinner/overlay/sun60i-a733-ili9486.dtbo` AND at
  `/boot/dtb/allwinner/overlay/sun60i-a733-ili9486.dtbo` (the ACTIVE path u-boot
  boot.scr uses — the dtb-6.6.98 one is the kernel-package copy; changes to the
  active overlay MUST go to /boot/dtb/allwinner/overlay/).
- `extraargs=fbcon=map:1` (NO `video=HDMI-A-1:d` — HDMI must be allowed to bind).
- `/etc/modules-load.d/ili9486.conf` must list `drm_mipi_dbi`, `ili9486`, `ads7846`
  (boot watchdog deletes it after 3 failed boots — restore it if missing).
- If a boot watchdog rollback marker exists (`/var/lib/ili9486-boot-count.rolled-back`),
  re-verify these invariants before assuming the machine is healthy.
- `scale = <1>` in the dtbo (native 480x320 panel; the DRM mode IS 480x320).
  `spi-max-frequency = <0x1e84800>` (32MHz — 40MHz glitched, do NOT retry).

### Failure history

1. **no-sunxi-drm overlay (2026-08-05)** — a session added `no-sunxi-drm` to `overlays=`
   to stop the mutter Wayland segfault. This VIOLATED the HDMI-first policy and would
   have permanently broken HDMI/USB-C. Reverted; the correct fix was moving the
   compositor off the SPI panel entirely (see Wayland below). NEVER disable sunxi-drm.
2. **40MHz SPI overclock** — glitched the panel; reverted to 32MHz. Hardware limit.
3. **Wrong overlay path** — dtbo changes went to /boot/dtb-6.6.98-sun60iw2/ which is
   NOT what u-boot loads. The ACTIVE dir is /boot/dtb/allwinner/overlay/.

## Current Architecture (2026-08-06)

The display stack is "virtual display + mirror":

- **X session (gdm) runs on HDMI only** (card0) — the SPI panel is NOT an X screen.
  Config: `/etc/X11/xorg.conf.d/10-lcd-modesetting.conf` (kmsdev card0, HDMI-1 at
  1280x720 via preferred modeline, `AccelMethod "none"`).
- **Xvfb :1** (`xvfb-virtual-display.service`) — virtual 1280x720 display, always
  present, used for headless GPU rendering when no HDMI is connected.
- **spi-mirror** (`spi-mirror.service` → `/usr/local/bin/spi-mirror-select.sh` →
  `/home/orangepi/spi-mirror`) — grabs the real X display root window (NOT :1 when
  HDMI is up), box-filters to 480x320, writes **XRGB8888 32bpp** (NOT RGB565 — that
  bug caused doubled images) into /dev/fb-lcd. Kernel shadow-diff pushes changed
  16x16 blocks over SPI. ~8fps.
- **Minecraft**: `minecraft/game-run.sh` auto-detects the live display (:0/:2/:1),
  runs PrismLauncher with `MESA_LOADER_DRIVER_OVERRIDE=zink MESA_GL_VERSION_OVERRIDE=4.5
  MESA_VK_WSI_DEBUG=sw ALSOFT_DRIVERS=null`. zink renders on the PowerVR GPU
  (GL→Vulkan); `MESA_VK_WSI_DEBUG=sw` forces Mesa's software-present fallback
  (XShmPutImage) because this X stack has NO DRI3 (glamor hangs on both card0 and
  card1 — hardware/driver lock, do not retry).

### Wayland

Weston on :1 worked but was removed ("Make X the default"). Old mutter-on-SPI
segfault (g_str_has_prefix NULL on card0 HDMI disconnect) is moot — the panel no
longer runs a compositor. Do not reintroduce mutter on the SPI panel.

### Touch / calibration

- ADS7846 touchscreen (`99-ads7846-calibration.conf`): `Floating "false"` (gnome-shell
  detaches it otherwise), TransformationMatrix identity, libinput Calibration Matrix
  from calibration.
- **Calibration**: `minecraft/calibration/calibrate-gui` — fullscreen crosshair GUI
  that waits indefinitely; touch & hold each corner on the PANEL. Reads real XI
  device range (65535) — the earlier bug was assuming 4095 (16x wrong scale, all
  touches collapsed to one corner). Saves profiles to
  `/home/orangepi/.config/touch-calib/{hdmi,fbcp}.{cal,ctm}`; apply via
  `/home/orangepi/touch-calib.sh apply hdmi|fbcp` or desktop shortcuts.
- `show-touches` — draws green crosshair where each touch lands (calibration check).
- Desktop shortcuts: `calibrate-touch-hdmi.desktop`, `calibrate-touch-fbcp.desktop`,
  `apply-touch-*.desktop`, `show-touches.desktop`.

## Gotchas

- **Session display number moves** (:0 → :2 after Xvfb grabs :1, and back).
  Everything must auto-detect the display (game-run.sh, spi-mirror-select.sh),
  never hardcode :0.
- **Xorg dual-screen config (card0+card1 as two X screens) breaks input** — the
  mouse/touch gets trapped on the SPI screen. Keep SPI out of X entirely.
- **gdm XAUTHORITY**: `/run/user/1000/gdm/Xauthority` (user session) or
  `/run/user/127/gdm/Xauthority` (greeter). Check which is current.
- **PrismLauncher refuses root**; launch via `su - orangepi -c` (launch-detached.sh).
- **Stale prismlauncher processes** hold the instance lock; kill all + rm
  `instances/26.1.2/instance.lock` before relaunch.
- **libopenal segfaults** enumerating devices with no audio server: `ALSOFT_DRIVERS=null`.
- **NeoForge early splash window kills zink** (needs GLX): `fml.toml`
  `earlyWindowControl = false`.
- **sudo**: `SUDO_ASKPASS=/home/orangepi/.opencode-askpass sudo -A <cmd>`.
- **Heredocs via `sudo -A bash -c` eat $variables** — write files with the Write
  tool, or single-quote the heredoc delimiter.
- **`pkill -f "Xvfb :1"` from a root shell can hang the shell** (matches itself);
  use exact PIDs or `pkill -x`.
