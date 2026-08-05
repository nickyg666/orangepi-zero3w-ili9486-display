/*
 * spi-mirror: mirror the primary X display (HDMI/USB-C) onto the ILI9486
 * SPI panel (card1, /dev/fb-lcd).
 *
 * The GPU renders everything natively on the external display (full DRI3 /
 * zink GLX present path). This daemon grabs the root window, box-filters it
 * down to the 480x320 panel resolution, and writes RGB565 into the ili9486
 * fbdev. The kernel shadow-diff driver only pushes changed 16x16 blocks
 * over SPI, so static content costs ~0 and motion costs one panel frame.
 *
 * Usage: spi-mirror [display]   (default :0)
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define PANEL_W 480
#define PANEL_H 320

int main(int argc, char **argv)
{
	const char *disp = argc > 1 ? argv[1] : NULL;
	Display *dpy = XOpenDisplay(disp);
	if (!dpy) {
		fprintf(stderr, "spi-mirror: cannot open display %s\n",
			disp ? disp : ":0");
		return 1;
	}

	Window root = DefaultRootWindow(dpy);
	int scr_w = DisplayWidth(dpy, DefaultScreen(dpy));
	int scr_h = DisplayHeight(dpy, DefaultScreen(dpy));

	int fd = open("/dev/fb-lcd", O_RDWR);
	if (fd < 0) {
		perror("spi-mirror: open /dev/fb-lcd");
		return 1;
	}
	struct fb_var_screeninfo var = {0};
	if (ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0) {
		perror("FBIOGET_VSCREENINFO");
		return 1;
	}
	uint16_t *panel = mmap(NULL, var.xres * var.yres * 2, PROT_READ | PROT_WRITE,
			       MAP_SHARED, fd, 0);
	if (panel == MAP_FAILED) {
		perror("mmap fb");
		return 1;
	}

	fprintf(stderr, "spi-mirror: %s %dx%d -> panel %dx%d\n", disp ? disp : ":0",
		scr_w, scr_h, var.xres, var.yres);

	/* linear RGBA buffer for the grabbed frame */
	uint32_t *fb = malloc((size_t)scr_w * scr_h * 4);
	if (!fb)
		return 1;

	unsigned frame = 0;
	for (;;) {
		XImage *img = XGetImage(dpy, root, 0, 0, scr_w, scr_h, AllPlanes, ZPixmap);
		if (!img) {
			fprintf(stderr, "XGetImage failed\n");
			sleep(2);
			continue;
		}
		memcpy(fb, img->data, (size_t)scr_w * scr_h * 4);
		XDestroyImage(img);

		/* box-filter scr_w x scr_h -> PANEL_W x PANEL_H, RGB565 big-endian wire order */
		for (int y = 0; y < var.yres; y++) {
			int y0 = y * scr_h / var.yres;
			int y1 = (y + 1) * scr_h / var.yres;
			if (y1 <= y0) y1 = y0 + 1;
			for (int x = 0; x < var.xres; x++) {
				int x0 = x * scr_w / var.xres;
				int x1 = (x + 1) * scr_w / var.xres;
				if (x1 <= x0) x1 = x0 + 1;
				uint32_t r = 0, g = 0, b = 0;
				unsigned n = 0;
				for (int sy = y0; sy < y1; sy++) {
					const uint32_t *row = fb + (size_t)sy * scr_w;
					for (int sx = x0; sx < x1; sx++) {
						uint32_t px = row[sx];
						b += px & 0xff;
						g += (px >> 8) & 0xff;
						r += (px >> 16) & 0xff;
						n++;
					}
				}
				r = (r + n / 2) / n; g = (g + n / 2) / n; b = (b + n / 2) / n;
				uint16_t rgb565 = ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
				panel[y * var.xres + x] = __builtin_bswap16(rgb565);
			}
		}

		if ((frame++ & 0x1f) == 0)
			fprintf(stderr, "spi-mirror: frame %u pushed\n", frame);
		usleep(100000);
	}
	return 0;
}
