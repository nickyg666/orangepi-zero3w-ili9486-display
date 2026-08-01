import subprocess, time, os, select, sys
p = subprocess.Popen(["evtest","/dev/input/event1"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
os.set_blocking(p.stdout.fileno(), False)
def grab(name):
    buf=b""; x=None; pts=[]
    print(f">>> Touch {name} edge and HOLD ~3s", flush=True)
    t0=time.time()
    while time.time()-t0 < 3.0:
        r,_,_ = select.select([p.stdout],[],[],0.1)
        if not r: continue
        data = os.read(p.stdout.fileno(), 4096)
        if not data: break
        buf += data
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            s = line.decode('utf-8','ignore')
            if "code 0 (ABS_X), value" in s: x=int(s.split("value")[-1])
            elif "code 1 (ABS_Y), value" in s and x is not None:
                pts.append((x,int(s.split("value")[-1]))); x=None
    print("    lift", flush=True)
    time.sleep(1)
    if pts:
        xs=[a for a,b in pts]; ys=[b for a,b in pts]
        return (min(xs),max(xs),min(ys),max(ys))
    return None
print("Touch these in order: (1) very TOP edge, (2) very BOTTOM edge, (3) LEFT edge, (4) RIGHT edge")
for name in ["TOP","BOTTOM","LEFT","RIGHT"]:
    r=grab(name)
    if r: print(f"    {name}: X[{r[0]},{r[1]}] Y[{r[2]},{r[3]}]", flush=True)
p.terminate()
