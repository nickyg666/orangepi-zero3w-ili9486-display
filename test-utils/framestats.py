import os, time, sys
def st(f):
    return int(open("/sys/bus/spi/devices/spi3.0/statistics/"+f).read())
fb=open("/dev/fb1","r+b")
data=bytes([0x0F]*(960*640*4))
fb.write(data); fb.flush(); time.sleep(2)
m1,t1,b1 = st("messages"), st("transfers"), st("bytes")
fb.seek(0); fb.write(data); fb.flush()
time.sleep(2)
m2,t2,b2 = st("messages"), st("transfers"), st("bytes")
print("one full-frame: +%d messages, +%d transfers, +%d bytes" % (m2-m1,t2-t1,b2-b1))
print("  ratio: %.1f transfers/message" % ((t2-t1)/(m2-m1 if m2>m1 else 1)))
