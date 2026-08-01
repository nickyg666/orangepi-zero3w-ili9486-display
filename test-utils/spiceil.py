import os, time, sys, threading
def spi(): return int(open("/sys/bus/spi/devices/spi3.0/statistics/bytes").read())
fb=open("/dev/fb1","r+b")
buf = bytes([0x0F, 0xF0]) * (480*320)
fb.seek(0); fb.write(buf); fb.flush()
time.sleep(2)
stop=False
def writer():
    while not stop:
        fb.seek(0); fb.write(buf); fb.flush()
th=threading.Thread(target=writer); th.start()
b0=spi(); t0=time.time()
time.sleep(5.0)
b1=spi(); t1=time.time()
stop=True; th.join()
dt=t1-t0
print("flooded for %.1fs: SPI moved %d bytes = %.0f B/s" % (dt, b1-b0, (b1-b0)/dt))
print("=> %.1f full-frames/s (307200 B each)" % ((b1-b0)/dt/307200))
