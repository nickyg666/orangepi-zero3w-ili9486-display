import os, time, sys
fb=open("/dev/fb1","r+b")
data=bytes([0x0F]*(960*640*4))
def spi(): return int(open("/sys/bus/spi/devices/spi3.0/statistics/bytes").read())
fb.write(data); fb.flush()
time.sleep(2)
for n in range(3):
    b0=spi()
    fb.seek(0); fb.write(data); fb.flush()
    t0=time.time()
    while time.time()-t0 < 5:
        if spi()-b0 >= 300000: break
        time.sleep(0.003)
    dt=time.time()-t0
    db=spi()-b0
    print("frame %d: %d bytes in %.0f ms => %.1f fps" % (n, db, dt*1000, db/dt/307200), flush=True)
