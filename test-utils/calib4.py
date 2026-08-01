import subprocess, time, select, os
p = subprocess.Popen(["evtest","/dev/input/event1"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
os.set_blocking(p.stdout.fileno(), False)
def sample(seconds):
    """return avg (x,y) and count over `seconds`"""
    buf=""; x=y=None; sx=sy=n=0
    t0=time.time()
    while time.time()-t0 < seconds:
        r,_,_=select.select([p.stdout],[],[],0.2)
        if not r: continue
        buf += os.read(p.stdout.fileno(),8192).decode("utf-8","ignore")
        while "\n" in buf:
            line,buf = buf.split("\n",1)
            if "code 0 (ABS_X), value" in line: x=int(line.split("value")[-1])
            elif "code 1 (ABS_Y), value" in line and x is not None:
                y=int(line.split("value")[-1]); sx+=x; sy+=y; n+=1
    return (sx/n, sy/n) if n else (None,None)
for name in ["TOP-LEFT","TOP-RIGHT","BOTTOM-RIGHT","BOTTOM-LEFT"]:
    print(f">>> Touch {name}, hold ~3s ...", flush=True)
    res = sample(3.5)
    print(f"    {name} avg = X={res[0]:.0f} Y={res[1]:.0f}  (keep holding for full 3s)", flush=True)
    # give a 2s rest between corners
    print("    lift, wait 2s", flush=True)
    time.sleep(2)
p.terminate()
