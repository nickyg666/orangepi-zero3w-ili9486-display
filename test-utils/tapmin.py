import subprocess, time, select, os
p = subprocess.Popen(["evtest","/dev/input/event1"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
os.set_blocking(p.stdout.fileno(), False)
def grab(name, secs):
    buf=b""; x=None; pts=[]
    print(f">>> Touch {name}, hold {secs}s", flush=True)
    t0=time.time()
    while time.time()-t0<secs:
        r,_,_=select.select([p.stdout],[],[],0.2)
        if not r: continue
        data=os.read(p.stdout.fileno(),4096)
        if not data: break
        buf+=data
        while b"\n" in buf:
            line,buf=buf.split(b"\n",1)
            s=line.decode('utf-8','ignore')
            if "code 0 (ABS_X), value" in s: x=int(s.split("value")[-1])
            elif "code 1 (ABS_Y), value" in s and x is not None:
                pts.append((x,int(s.split("value")[-1]))); x=None
    print("    lift", flush=True)
    time.sleep(1.2)
    if pts:
        xs=[a for a,b in pts]; ys=[b for a,b in pts]
        return (min(xs),max(xs),min(ys),max(ys))
    return None
print("Touch BOTTOM edge (hold ~4s)", flush=True)
r=grab("BOTTOM",4)
print("BOTTOM raw X[min,max] Y[min,max]:", r, flush=True)
print("Touch TOP edge (hold ~4s)", flush=True)
r=grab("TOP",4)
print("TOP raw:", r, flush=True)
p.terminate()
