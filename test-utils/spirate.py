import os, time, sys, glob
fb = None
for f in glob.glob("/sys/class/graphics/fb*"):
    try:
        if open(f+"/name").read().strip()=="ili9486drmfb":
            fb="/dev/"+os.path.basename(f); break
    except: pass
if not fb: print("no lcd fb"); sys.exit(1)
def spi(): return int(open("/sys/bus/spi/devices/spi3.0/statistics/bytes").read())
# measure sustained SPI drain with small writes (avoids blocking on full fb)
fh=open(fb,"r+b")
data=bytes([0x0F]*(960*64*4))  # 64 rows
b0=spi(); t0=time.time()
for i in range(20):
    fh.seek(0); fh.write(data); fh.flush()
time.sleep(1.5)  # let SPI drain
b1=spi(); dt=time.time()-t0
db=b1-b0
print("SPI drained %d bytes in %.1fs = %.0f KB/s => %.1f fps (RGB565 307KB)" % (db,dt,db/dt/1024,db/dt/307200), flush=True)
