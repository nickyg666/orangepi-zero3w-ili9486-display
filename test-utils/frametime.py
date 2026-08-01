import os, time, sys, glob
fb=None
for f in glob.glob("/sys/class/graphics/fb*"):
    try:
        if open(f+"/name").read().strip()=="ili9486drmfb": fb="/dev/"+os.path.basename(f); break
    except: pass
if not fb: print("no fb"); sys.exit(1)
def spi(): return int(open("/sys/bus/spi/devices/spi3.0/statistics/bytes").read())
sz=os.path.getsize(fb); target=sz//2
fh=open(fb,"r+b"); data=bytes([0x0F]*sz)
for n in range(3):
    b0=spi(); fh.seek(0); fh.write(data); fh.flush()
    t0=time.time(); db=0
    while time.time()-t0<5:
        db=spi()-b0
        if db>=target: break
        time.sleep(0.004)
    dt=time.time()-t0
    if db>0: print("frame %d: %d bytes in %.0f ms = %.1f fps" % (n,db,dt*1000,db/dt/target),flush=True)
    else: print("frame %d: no activity" % n, flush=True)
