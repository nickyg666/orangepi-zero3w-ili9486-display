import subprocess, time, select, os
p = subprocess.Popen(["evtest","/dev/input/event1"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
os.set_blocking(p.stdout.fileno(), False)
def sample_one(name, seconds):
    """Sample while BTN_TOUCH==1 during the window; average the press samples."""
    buf=""; x=y=None; down=False
    sx=sy=n=0
    t0=time.time()
    print(f">>> Touch {name} and HOLD until told to lift...", flush=True)
    while time.time()-t0 < seconds:
        r,_,_=select.select([p.stdout],[],[],0.2)
        if not r: continue
        buf += os.read(p.stdout.fileno(),8192).decode("utf-8","ignore")
        while "\n" in buf:
            line,buf = buf.split("\n",1)
            if "BTN_TOUCH), value 1" in line: down=True
            elif "BTN_TOUCH), value 0" in line: down=False
            elif "code 0 (ABS_X), value" in line and down:
                x=int(line.split("value")[-1])
            elif "code 1 (ABS_Y), value" in line and down and x is not None:
                sx+=x; sy+=int(line.split("value")[-1]); n+=1
    p.terminate()
    if n: return (sx/n, sy/n, n)
    return (None,None,0)
