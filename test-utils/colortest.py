import os, time, sys
fb=open("/dev/fb1","r+b")
def solid(r,g,b):
    px = bytes([b,g,r,0])
    return px * (960*640)
for name,(r,g,b) in [("RED",(255,0,0)),("GREEN",(0,255,0)),("BLUE",(0,0,255)),
                       ("YELLOW",(255,255,0)),("CYAN",(0,255,255)),("MAGENTA",(255,0,255)),
                       ("WHITE",(255,255,255)),("BLACK",(0,0,0))]:
    fb.seek(0); fb.write(solid(r,g,b)); fb.flush()
    print("now showing:", name, flush=True)
    time.sleep(3)
