import subprocess, time, os, sys
p = subprocess.Popen(["evtest","/dev/input/event1"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
os.set_blocking(p.stdout.fileno(), False)
def grab(name):
    buf=b""; x=None; pts=[]
    print(f">>> Touch {name}, HOLD ~3s", flush=True)
    t0=time.time()
    while time.time()-t0 < 3.0:
        r,_,_ = select.select([p.stdout],[],[],0.1)
        if not r: continue
        data = os.read(p.stdout.fileno(), 4096)
        if not data: break
        buf += data
        lines = buf.split(b"\n"); buf = lines[-1]
        for line in lines[:-1]:
            s = line.decode('utf-8','ignore')
            if "code 0 (ABS_X), value" in s: x=int(s.split("value")[-1])
            elif "code 1 (ABS_Y), value" in s and x is not None:
                pts.append((x,int(s.split("value")[-1]))); x=None
    print("    lift", flush=True)
    time.sleep(1.0)
    if pts:
        xs=[p0 for p0,p1 in pts]; ys=[p1 for p0,p1 in pts]
        return (sum(xs)//len(xs), sum(ys)//len(ys))
    return None
import select
corners=[]
for name in ["TOP-LEFT","TOP-RIGHT","BOTTOM-RIGHT","BOTTOM-LEFT"]:
    r=grab(name)
    if r: corners.append(r); print(f"    {name}: X={r[0]} Y={r[1]}", flush=True)
    else: print(f"    {name}: FAILED", flush=True)
p.terminate()
print("CORNERS:", corners)
sys.stdout.flush()
