import subprocess, time, select, os
p = subprocess.Popen(["evtest","/dev/input/event1"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
os.set_blocking(p.stdout.fileno(), False)
def grab(name):
    buf=""; x=None; xs=[]; ys=[]
    print(f">>> Touch {name} corner, HOLD until told to lift (~2.5s)", flush=True)
    t0=time.time()
    while time.time()-t0 < 3.2:
        r,_,_=select.select([p.stdout],[],[],0.2)
        if not r: continue
        buf += os.read(p.stdout.fileno(),8192).decode("utf-8","ignore")
        while "\n" in buf:
            line,buf = buf.split("\n",1)
            if "code 0 (ABS_X), value" in line: x=int(line.split("value")[-1])
            elif "code 1 (ABS_Y), value" in line and x is not None:
                xs.append(x); ys.append(int(line.split("value")[-1]))
    print("    lift now", flush=True)
    time.sleep(1.2)
    if xs:
        return (sum(xs)//len(xs), sum(ys)//len(ys))
    return None
corners=[]
for name in ["TOP-LEFT","TOP-RIGHT","BOTTOM-RIGHT","BOTTOM-LEFT"]:
    r = grab(name)
    if r: corners.append(r); print(f"    {name}: X={r[0]} Y={r[1]}", flush=True)
    else: print(f"    {name}: FAILED", flush=True)
p.terminate()
print("CORNERS:", corners)
