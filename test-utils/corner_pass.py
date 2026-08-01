import subprocess, time, select, os
p = subprocess.Popen(["evtest","/dev/input/event1"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
os.set_blocking(p.stdout.fileno(), False)
def grab(name, seconds):
    buf=""; x=None; y=None; pts=[]
    t0=time.time()
    print(f">>> Touch {name}, HOLD 2s, lift", flush=True)
    while time.time()-t0 < seconds:
        r,_,_=select.select([p.stdout],[],[],0.2)
        if not r: continue
        buf += os.read(p.stdout.fileno(),8192).decode("utf-8","ignore")
        while "\n" in buf:
            line,buf = buf.split("\n",1)
            if "code 0 (ABS_X), value" in line: x=int(line.split("value")[-1])
            elif "code 1 (ABS_Y), value" in line and x is not None:
                pts.append((x,int(line.split("value")[-1])))
    if pts:
        xs=[pt[0] for pt in pts]; ys=[pt[1] for pt in pts]
        print(f"    {name}: n={len(pts)} X:[{min(xs)},{max(xs)}] Y:[{min(ys)},{max(ys)}]  mid=({sum(xs)//len(xs)},{sum(ys)//len(ys)})", flush=True)
    else:
        print(f"    {name}: NO samples", flush=True)
    time.sleep(1.5)
for c in ["TOP-LEFT","TOP-RIGHT","BOTTOM-RIGHT","BOTTOM-LEFT"]:
    grab(c, 3.0)
p.terminate()
