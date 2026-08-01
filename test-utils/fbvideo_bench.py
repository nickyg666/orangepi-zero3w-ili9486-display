import os, time, sys, array
# Direct framebuffer video benchmark - RetroPie style, writes RGB565 to fb at native res
def spi():
    return int(open("/sys/bus/spi/devices/spi3.0/statistics/bytes").read())
fb=open("/dev/fb1","r+b")
# read current fb geometry
import fcntl, struct
print("fb1 size:", os.path.getsize("/dev/fb1"), file=sys.stderr)
# Try writing a native 480x320 RGB565 frame (2 bytes/px) - if fb is 960x640 XRGB that's the box-filter path
# Write raw pattern and measure SPI drain
buf = bytes([0x0F, 0xF0]) * (480*320)  # 307200 bytes native frame
# warm
fb.seek(0); fb.write(buf); fb.flush()
time.sleep(1)
# measure sustained
b0 = spi(); t0 = time.time()
end = t0 + 3.0
frames = 0
while time.time() < end:
    fb.seek(0); fb.write(buf); fb.flush()
    frames += 1
b1 = spi(); t1 = time.time()
dt = t1-t0
print("direct-fb writes: %d frames in %.1fs (%.1f fps)" % (frames, dt, frames/dt))
print("SPI: %d bytes (%.0f B/s)" % (b1-b0, (b1-b0)/dt))
