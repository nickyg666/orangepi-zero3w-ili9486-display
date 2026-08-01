import subprocess, time, os, select, sys, re
p = subprocess.Popen(["xinput","test-xi2","--root","8"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
os.set_blocking(p.stdout.fileno(), False)
print("TOUCH BOTTOM edge of screen and HOLD 4s, then lift", flush=True)
rx=re.compile(r'^\s+(\d+):\s+([\d.]+)')
vals={}; buf=b""; t0=time.time()
samples=[]
while time.time()-t0<8:
    r,_,_=select.select([p.stdout],[],[],0.2)
    if not r: continue
    data=os.read(p.stdout.fileno(),4096)
    if not data: break
    buf+=data
    while b"\n" in buf:
        line,buf=buf.split(b"\n",1)
        s=line.decode('utf-8','ignore')
        if 'RawTouchBegin' in s: vals={}
        elif 'RawTouchEnd' in s: vals={}
        m=rx.match(s)
        if m and m.group(1) in ('0','1'):
            vals[int(m.group(1))]=float(m.group(2))
        if len(vals)>=2:
            samples.append((vals[0],vals[1]))
if samples:
    xs=[a for a,b in samples]; ys=[b for a,b in samples]
    print("BOTTOM touch: X[%d..%d] Y[%d..%d] (65535 space)" % (min(xs),max(xs),min(ys),max(ys)), flush=True)
else:
    print("no samples", flush=True)
p.terminate()
