#!/usr/bin/env python3
import gi, subprocess, threading, queue, re, sys, os
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk, Gdk, GLib

MATRIX = None
if os.environ.get("TV_MATRIX"):
    MATRIX = [float(x) for x in os.environ["TV_MATRIX"].split(",")]
    assert len(MATRIX) == 9

class TouchView(Gtk.Window):
    def __init__(self):
        Gtk.Window.__init__(self, type=Gtk.WindowType.POPUP)
        self.set_decorated(False)
        self.set_skip_taskbar_hint(True)
        self.set_skip_pager_hint(True)
        self.set_accept_focus(False)
        self.set_app_paintable(True)
        self.set_size_request(960, 640)
        self.set_position(Gtk.WindowPosition.CENTER)
        self.set_visual(Gdk.Screen.get_default().get_rgba_visual())
        self.x = self.y = -100
        self.down = False
        self.q = queue.Queue()
        self.add_events(Gdk.EventMask.KEY_PRESS_MASK)
        self.connect("draw", self.on_draw)
        self.connect("destroy", Gtk.main_quit)
        self.connect("key-press-event", self.on_key)
        css = Gtk.CssProvider()
        css.load_from_data(b"window { background-color: rgba(0,0,0,0.3); }")
        Gtk.StyleContext.add_provider_for_screen(Gdk.Screen.get_default(), css, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
        self.show_all()
        GLib.timeout_add(16, self.poll)
        threading.Thread(target=self.worker, daemon=True).start()
    def on_key(self, w, e):
        if e.keyval == 0xff1b: Gtk.main_quit()
        return False
    def apply_matrix(self, rx, ry):
        # rx,ry in 0..65535. Apply 3x3 affine to normalized [0,1].
        u = rx/65535.0; v = ry/65535.0
        if MATRIX:
            x = MATRIX[0]*u + MATRIX[1]*v + MATRIX[2]
            y = MATRIX[3]*u + MATRIX[4]*v + MATRIX[5]
        else:
            x, y = u, v
        return x*960, y*640
    def on_draw(self, w, cr):
        cr.set_source_rgba(0,0,0,0); cr.paint()
        if self.x >= 0:
            cr.set_source_rgba(1,0,0,0.9); cr.set_line_width(4)
            cr.arc(self.x, self.y, 18, 0, 6.283); cr.stroke()
            cr.set_source_rgba(0,1,0,1); cr.set_line_width(3)
            cr.move_to(self.x-18, self.y); cr.line_to(self.x+18, self.y)
            cr.move_to(self.x, self.y-18); cr.line_to(self.x, self.y+18)
            cr.stroke()
            cr.set_source_rgba(1,1,1,0.9)
            cr.select_font_face("Sans", 0, 0)
            cr.set_font_size(14)
            cr.move_to(self.x+24, self.y-24)
            cr.show_text("(%d,%d) %s" % (self.x, self.y, "DOWN" if self.down else "up"))
        return False
    def poll(self):
        try:
            while True:
                typ, v = self.q.get_nowait()
                if typ == 'm': self.x, self.y = v
                elif typ == 'd': self.down = True
                elif typ == 'u': self.down = False
                self.queue_draw()
        except queue.Empty: pass
        return True
    def worker(self):
        proc = subprocess.Popen(["xinput","test-xi2","--root","8"],
                                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        rx = re.compile(r'^\s+(\d+):\s+([\d.]+)')
        vals = {}
        while True:
            line = proc.stdout.readline()
            if not line: break
            line = line.decode('utf-8','ignore').rstrip()
            if 'RawTouchBegin' in line:
                self.q.put(('d', None))
            elif 'RawTouchEnd' in line:
                self.q.put(('u', None))
                vals = {}
            m = rx.match(line)
            if m and m.group(1) in ('0','1'):
                vals[int(m.group(1))] = float(m.group(2))
            if len(vals) >= 2:
                sx, sy = self.apply_matrix(vals[0], vals[1])
                self.q.put(('m', (min(max(int(sx),0),959), min(max(int(sy),0),639))))

app = TouchView()
Gtk.main()
