/*
 * calibrate-gui: fullscreen touch calibration for the ILI9486 panel.
 *
 * Draws a crosshair on the HDMI display at each corner; the SPI panel shows
 * the same content via spi-mirror. Touch the PANEL at the crosshair and
 * hold - the raw device coordinates are recorded. After all 4 corners the
 * affine calibration (scale + offset + rotation) is computed from the
 * REAL measured touch range, applied via xinput, and saved to
 * ~/.config/touch-calib/{hdmi,fbcp}.{cal,ctm}.
 *
 * No timeouts - it waits for input indefinitely. Esc aborts.
 *
 * Usage: calibrate-gui [hdmi|fbcp] [display]
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/XInput2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>

static Display *dpy;
static Window win;
static GC gc;
static int xi_opcode;

#define NUM_CORNERS 4

struct corner { const char *name; };

static void draw_crosshair(int cx, int cy, const char *label, int which)
{
	int r = 36;
	XSetWindowBackground(dpy, win, 0x101010);
	XClearWindow(dpy, win);
	XSetForeground(dpy, gc, 0x00ff00);
	XSetLineAttributes(dpy, gc, 4, LineSolid, CapRound, JoinRound);
	XDrawArc(dpy, win, gc, cx - r, cy - r, r * 2, r * 2, 0, 360 * 64);
	XDrawLine(dpy, win, gc, cx - r - 10, cy, cx + r + 10, cy);
	XDrawLine(dpy, win, gc, cx, cy - r - 10, cx, cy + r + 10);

	XSetForeground(dpy, gc, 0xffffff);
	XSetLineAttributes(dpy, gc, 1, LineSolid, CapButt, JoinMiter);
	char msg[128];
	snprintf(msg, sizeof msg, "%d / %d  -  %s  (touch the PANEL here, hold)",
		 which + 1, NUM_CORNERS, label);
	XDrawString(dpy, win, gc, 20, 40, msg, (int)strlen(msg));
	XFlush(dpy);
}

static void draw_done(const char *msg)
{
	XSetWindowBackground(dpy, win, 0x102010);
	XClearWindow(dpy, win);
	XSetForeground(dpy, gc, 0x00ff00);
	XSetLineAttributes(dpy, gc, 1, LineSolid, CapButt, JoinMiter);
	XDrawString(dpy, win, gc, 20, 40, msg, (int)strlen(msg));
	XFlush(dpy);
}

/* average a held touch: returns number of samples */
static int sample_touch(int devid, double *x, double *y, int *xmax, int *ymax)
{
	double sx = 0, sy = 0;
	int n = 0;
	/* accumulate until the touch is released */
	int pressed = 0;
	double lastx = 0, lasty = 0;
	XEvent xev;
	while (1) {
		XNextEvent(dpy, &xev);
		if (xev.type != GenericEvent)
			continue;
		XGenericEventCookie *cookie = &xev.xcookie;
		if (cookie->extension != xi_opcode)
			continue;
		if (!XGetEventData(dpy, cookie))
			continue;
		switch (cookie->evtype) {
		case XI_RawButtonPress:
			/* libinput may not deliver raw press for BTN_TOUCH;
			 * if we already have samples, finalize immediately */
			if (n > 0) {
				*x = sx / n;
				*y = sy / n;
				XFreeEventData(dpy, cookie);
				return n;
			}
			pressed = 1;
			break;
		case XI_RawButtonRelease:
			pressed = 0;
			if (n > 0) {
				*x = sx / n;
				*y = sy / n;
				XFreeEventData(dpy, cookie);
				return n;
			}
			break;
		case XI_RawMotion:
			{
				XIRawEvent *re = cookie->data;
				/* raw axis values; device has X,Y(+pressure) */
				unsigned int idx = 0;
				for (int ax = 0; ax < 2; ax++) {
					if (XIMaskIsSet(re->valuators.mask, ax)) {
						if (ax == 0) lastx = re->raw_values[idx];
						if (ax == 1) lasty = re->raw_values[idx];
						idx++;
					}
				}
				/* always record (some stacks send motion w/o press) */
				sx += lastx;
				sy += lasty;
				n++;
				if (pressed && n > 60) {
					/* enough samples, stop early */
					*x = sx / n;
					*y = sy / n;
					XFreeEventData(dpy, cookie);
					return n;
				}
			}
			break;
		case XI_RawKeyPress:
		case XI_RawKeyRelease:
			{
				XIRawEvent *re = cookie->data;
				/* Esc = abort */
				if (re->detail == 0x09) {
					XFreeEventData(dpy, cookie);
					return -1;
				}
			}
			break;
		}
		XFreeEventData(dpy, cookie);
	}
}

int main(int argc, char **argv)
{
	const char *profile = argc > 1 ? argv[1] : "hdmi";
	const char *disp = argc > 2 ? argv[2] : NULL;
	dpy = XOpenDisplay(disp);
	if (!dpy) {
		fprintf(stderr, "cannot open display %s\n", disp ? disp : ":0");
		return 1;
	}

	if (!XQueryExtension(dpy, "XInputExtension", &xi_opcode, &xi_opcode, &xi_opcode)) {
		fprintf(stderr, "no XInput2\n");
		return 1;
	}

	/* find ADS7846 */
	int ndev = 0;
	XIDeviceInfo *devs = XIQueryDevice(dpy, XIAllDevices, &ndev);
	int devid = -1;
	XIValuatorClassInfo *val = NULL;
	for (int i = 0; i < ndev; i++) {
		if (devs[i].use == XISlavePointer &&
		    strstr(devs[i].name, "ADS7846")) {
			devid = devs[i].deviceid;
			for (int c = 0; c < devs[i].num_classes; c++) {
				if (devs[i].classes[c]->type == XIValuatorClass) {
					val = (XIValuatorClassInfo *)devs[i].classes[c];
					break;
				}
			}
			break;
		}
	}
	if (devid < 0 || !val) {
		fprintf(stderr, "ADS7846 not found\n");
		return 1;
	}
	int xmax = val->max, ymax = val->max;

	/* fullscreen override-redirect window */
	Screen *sc = DefaultScreenOfDisplay(dpy);
	int sw = DisplayWidth(dpy, DefaultScreen(dpy));
	int sh = DisplayHeight(dpy, DefaultScreen(dpy));
	XSetWindowAttributes attrs = {0};
	attrs.override_redirect = True;
	attrs.event_mask = ExposureMask | KeyPressMask;
	win = XCreateWindow(dpy, RootWindow(dpy, DefaultScreen(dpy)),
			    0, 0, sw, sh, 0,
			    DefaultDepth(dpy, DefaultScreen(dpy)),
			    InputOutput, CopyFromParent,
			    CWOverrideRedirect | CWEventMask, &attrs);
	XMapRaised(dpy, win);
	gc = XCreateGC(dpy, win, 0, NULL);

	/* select raw events from the touch device on the root */
	XIEventMask mask;
	unsigned char data[XIMaskLen(XI_LASTEVENT)] = {0};
	mask.deviceid = devid;
	mask.mask_len = sizeof(data);
	mask.mask = data;
	XISetMask(mask.mask, XI_RawButtonPress);
	XISetMask(mask.mask, XI_RawButtonRelease);
	XISetMask(mask.mask, XI_RawMotion);
	XISetMask(mask.mask, XI_RawKeyPress);
	XISetMask(mask.mask, XI_RawKeyRelease);
	XISelectEvents(dpy, RootWindow(dpy, DefaultScreen(dpy)), &mask, 1);
	XSelectInput(dpy, win, ExposureMask | KeyPressMask);

	/* corner screen positions */
	int cx[NUM_CORNERS] = { 40, sw - 40, sw - 40, 40 };
	int cy[NUM_CORNERS] = { 40, 40, sh - 40, sh - 40 };
	const char *names[NUM_CORNERS] = {
		"TOP-LEFT", "TOP-RIGHT", "BOTTOM-RIGHT", "BOTTOM-LEFT"
	};

	fprintf(stderr, "calibrating '%s' on %dx%d, device %d (X max %d)\n",
		profile, sw, sh, devid, xmax);

	double pts[NUM_CORNERS][2];
	for (int i = 0; i < NUM_CORNERS; i++) {
		draw_crosshair(cx[i], cy[i], names[i], i);
		double rx = 0, ry = 0;
		int n = sample_touch(devid, &rx, &ry, &xmax, &ymax);
		if (n < 0) {
			fprintf(stderr, "aborted\n");
			return 1;
		}
		if (n == 0) {
			fprintf(stderr, "no samples for %s - try again\n", names[i]);
			i--;
			continue;
		}
		pts[i][0] = rx;
		pts[i][1] = ry;
		fprintf(stderr, "%s: raw X=%.0f Y=%.0f (n=%d)\n",
			names[i], rx, ry, n);
		/* small pause so the user can reposition */
		usleep(800000);
	}

	XFree(devs);

	double xmin_r = 1e9, xmax_r = -1e9, ymin_r = 1e9, ymax_r = -1e9;
	for (int i = 0; i < NUM_CORNERS; i++) {
		if (pts[i][0] < xmin_r) xmin_r = pts[i][0];
		if (pts[i][0] > xmax_r) xmax_r = pts[i][0];
		if (pts[i][1] < ymin_r) ymin_r = pts[i][1];
		if (pts[i][1] > ymax_r) ymax_r = pts[i][1];
	}
	if (xmax_r <= xmin_r || ymax_r <= ymin_r) {
		fprintf(stderr, "bad measurements\n");
		return 1;
	}

	/* same math as calibrate_touch.py, device range from XI */
	double sx_scale = (double)(xmax + 1) / (xmax_r - xmin_r);
	double sy_scale = (double)(ymax + 1) / (ymax_r - ymin_r);
	double cal[9] = { 0, sx_scale, -ymin_r / (ymax_r - ymin_r),
			  -sy_scale, 0, xmax_r / (xmax_r - xmin_r),
			  0, 0, 1 };
	char cals[128];
	snprintf(cals, sizeof cals, "%.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f",
		 cal[0], cal[1], cal[2], cal[3], cal[4], cal[5],
		 cal[6], cal[7], cal[8]);

	fprintf(stderr, "raw X [%.0f, %.0f]  Y [%.0f, %.0f]\n",
		xmin_r, xmax_r, ymin_r, ymax_r);
	fprintf(stderr, "libinput Calibration Matrix: %s\n", cals);

	/* apply via xinput */
	char cmd[512];
	const char *auth = getenv("XAUTHORITY");
	if (!auth) auth = "/run/user/1000/gdm/Xauthority";
	snprintf(cmd, sizeof cmd,
		 "DISPLAY=%s XAUTHORITY=%s xinput set-prop '%s' "
		 "'Coordinate Transformation Matrix' 1 0 0 0 1 0 0 0 1",
		 DisplayString(dpy), auth, "ADS7846 Touchscreen");
	system(cmd);
	snprintf(cmd, sizeof cmd,
		 "DISPLAY=%s XAUTHORITY=%s xinput set-prop '%s' "
		 "'libinput Calibration Matrix' %s",
		 DisplayString(dpy), auth, "ADS7846 Touchscreen", cals);
	system(cmd);

	/* save profiles */
	char path[512];
	struct passwd *pw = getpwuid(getuid());
	snprintf(path, sizeof path, "%s/.config/touch-calib", pw->pw_dir);
	mkdir(path, 0755);
	FILE *f;
	char fpath[640];
	for (int p = 0; p < 2; p++) {
		const char *pr = p == 0 ? "hdmi" : "fbcp";
		snprintf(fpath, sizeof fpath, "%s/%s.ctm", path, pr);
		f = fopen(fpath, "w");
		if (f) { fprintf(f, "1 0 0 0 1 0 0 0 1\n"); fclose(f); }
		snprintf(fpath, sizeof fpath, "%s/%s.cal", path, pr);
		f = fopen(fpath, "w");
		if (f) { fprintf(f, "%s\n", cals); fclose(f); }
	}

	draw_done("Calibration complete - touch anywhere to close");
	/* wait for one more touch to dismiss */
	{
		double rx, ry;
		sample_touch(devid, &rx, &ry, &xmax, &ymax);
	}
	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);
	return 0;
}
