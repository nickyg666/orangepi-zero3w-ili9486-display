import subprocess, select, os, time
# watch X pointer position via xinput test-xi2 on master pointer
p = subprocess.Popen(["xinput","test-xi2","--root"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
os.set_blocking(p.stdout.fileno(), False)
print("Watching pointer. Touch the CENTER of the screen and hold ~3s.")
buf = ""
t0 = time.time()
# detect Motion absolute events, print value 0/1 (x/y)
while time.time()-t0 < 15:
    r,_,_ = select.select([p.stdout],[],[],0.2)
    if not r: continue
    chunk = os.read(p.stdout.fileno(), 8192).decode("utf-8","ignore")
    buf += chunk
    lines = buf.split("\n")
    buf = lines[-1]
    for line in lines[:-1]:
        if "value" in line and ("valuator" in line.lower() or "detail for valuator" in line):
            pass
        if line.strip().startswith("value") and (" 0:" in line or " 1:" in line):
            print(line.strip())
p.terminate()
