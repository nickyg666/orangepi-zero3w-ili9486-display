/*
 * show-touches: visualize where touch input lands on the X display.
 *
 * Draws a crosshair ring at the current pointer position whenever the
 * ADS7846 touchscreen reports a touch. Lets you verify calibration:
 * touch a spot on the panel -> the ring shows where the system mapped it
 * on the HDMI display.
 *
 * Usage: show-touches [display]  (default :0, XAUTHORITY from env)
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XInput2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Display *dpy;
static Window root;
static GC gc;

static void draw_ring(int x, int y)
{
	int r = 24;
	XSetForeground(dpy, gc, 0x00ff00);
	XDrawArc(dpy, root, gc, x - r, y - r, r * 2, r * 2, 0, 360 * 64);
	XDrawLine(dpy, root, gc, x - r - 6, y, x + r + 6, y);
	XDrawLine(dpy, root, gc, x, y - r - 6, x, y + r + 6);
	XFlush(dpy);
}

static void clear_ring(void)
{
	XClearArea(dpy, root, 0, 0, 0, 0, False);
	XFlush(dpy);
}

int main(int argc, char **argv)
{
	const char *disp = argc > 1 ? argv[1] : NULL;
	dpy = XOpenDisplay(disp);
	if (!dpy) {
		fprintf(stderr, "show-touches: cannot open display %s\n",
			disp ? disp : ":0");
		return 1;
	}
	root = DefaultRootWindow(dpy);

	int ev, err, major = 2, minor = 0;
	if (!XQueryExtension(dpy, "XInputExtension", &ev, &err, &minor)) {
		fprintf(stderr, "no XInput2\n");
		return 1;
	}

	int n = 0;
	XIDeviceInfo *devs = XIQueryDevice(dpy, XIAllDevices, &n);
	int devid = -1;
	for (int i = 0; i < n; i++) {
		if (devs[i].use == XISlavePointer &&
		    strstr(devs[i].name, "ADS7846")) {
			devid = devs[i].deviceid;
			break;
		}
	}
	if (devid < 0) {
		fprintf(stderr, "ADS7846 not found; devices:\n");
		for (int i = 0; i < n; i++)
			fprintf(stderr, "  %d %s\n", devs[i].deviceid, devs[i].name);
		return 1;
	}
	fprintf(stderr, "watching device %d (ADS7846)\n", devid);

	XIEventMask mask;
	unsigned char data[XIMaskLen(XI_LASTEVENT)] = {0};
	mask.deviceid = devid;
	mask.mask_len = sizeof(data);
	mask.mask = data;
	XISetMask(mask.mask, XI_Motion);
	XISetMask(mask.mask, XI_ButtonPress);
	XISetMask(mask.mask, XI_ButtonRelease);
	if (XISelectEvents(dpy, root, &mask, 1) != Success) {
		fprintf(stderr, "select events failed\n");
		return 1;
	}

	gc = XCreateGC(dpy, root, 0, NULL);
	XSetLineAttributes(dpy, gc, 3, LineSolid, CapRound, JoinRound);
	XSetForeground(dpy, gc, 0x00ff00);

	fprintf(stderr, "show-touches: running (touch the panel, watch the HDMI screen)\n");

	XEvent xev;
	for (;;) {
		XNextEvent(dpy, &xev);
		if (xev.type != GenericEvent)
			continue;
		XGenericEventCookie *cookie = &xev.xcookie;
		if (cookie->extension != ev)
			continue;
		if (!XGetEventData(dpy, cookie))
			continue;
		if (cookie->evtype == XI_Motion ||
		    cookie->evtype == XI_ButtonPress) {
			XIDeviceEvent *idev = cookie->data;
			if (idev->deviceid == devid)
				draw_ring((int)idev->event_x, (int)idev->event_y);
		} else if (cookie->evtype == XI_ButtonRelease) {
			clear_ring();
		}
		XFreeEventData(dpy, cookie);
	}
	return 0;
}
