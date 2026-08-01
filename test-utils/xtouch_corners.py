import subprocess, time, os, select, sys, re
p = subprocess.Popen(["xinput","test-xi2","--root","8"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
os.set_blocking(p.stdout.fileno(), False)
rx=re.compile(r'^\s+(\d+):\s+([\d.]+)')
def grab(name, secs):
    vals={}; buf=b""; pts=[]; t0=time.time()
    print(f">>> Touch {name}, hold ~{secs}s", flush=True)
    while time.time()-t0<secs:
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
            if m and m.group(1) in ('0','1'): vals[int(m.group(1))]=float(m.group(2))
            if len(vals)>=2:
                pts.append((vals[0],vals[1]))
    print("    lift", flush=True); time.sleep(1)
    if pts:
        xs=[a for a,b in pts]; ys=[b for a,b in pts]
        return (min(xs),max(xs),min(ys),max(ys))
    return None
print("Touch the 4 CORNERS in order (TL, TR, BR, BL), hold each ~3s", flush=True)
for name in ["TOP-LEFT","TOP-RIGHT","BOTTOM-RIGHT","BOTTOM-LEFT"]:
    r=grab(name,3)
    if r: print(f"    {name}: X[{r[0]:.0f},{r[1]:.0f}] Y[{r[2]:.0f},{r[3]:.0f}]", flush=True)
    else: print(f"    {name}: no samples", flush=True)
p.terminate()
