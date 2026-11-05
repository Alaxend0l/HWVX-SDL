#!/usr/bin/env python3
"""Speaks the game's protocol over wss:// to a nettest host behind cloudflared,
until the game's own client has TLS. Takes about 3 minutes; the URL is public, so
use a random room code.

  nettest host 47815 ROOM 99 260 &
  cloudflared tunnel --no-autoupdate --url http://127.0.0.1:47815
  # wait about 15 s after it prints the URL: looked up sooner, the name can fail
  # to resolve for over a minute
  tools/net_tunnel_check.py https://NAME.trycloudflare.com ROOM"""
import base64, os, socket, ssl, struct, sys, time, urllib.request, urllib.error
url,room=sys.argv[1],sys.argv[2].encode(); host=url.split("//")[1]
ctx=ssl.create_default_context(cafile="/etc/ssl/cert.pem")
def connect():
    s=ctx.wrap_socket(socket.create_connection((host,443),timeout=20),server_hostname=host)
    s.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1)
    k=base64.b64encode(os.urandom(16)).decode()
    s.sendall(f"GET /hwvx HTTP/1.1\r\nHost: {host}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: {k}\r\nSec-WebSocket-Version: 13\r\n\r\n".encode())
    b=b""
    while b"\r\n\r\n" not in b:
        c=s.recv(1)
        if not c: raise EOFError
        b+=c
    if b" 101 " not in b.split(b"\r\n")[0]: raise RuntimeError(b.split(b"\r\n")[0])
    return s
def rn(s,n):
    b=bytearray()
    while len(b)<n:
        c=s.recv(n-len(b))
        if not c: raise EOFError
        b+=c
    return bytes(b)
def rf(s):
    h=rn(s,2); op=h[0]&15; n=h[1]&127
    assert not h[1]&128, "server frame was masked"
    if n==126:n=struct.unpack(">H",rn(s,2))[0]
    elif n==127:n=struct.unpack(">Q",rn(s,8))[0]
    return op,bool(h[0]&128),rn(s,n)
def wf(s,op,d):
    n=len(d);h=bytes([0x80|op])
    if n<126:h+=bytes([0x80|n])
    elif n<65536:h+=bytes([0xFE])+struct.pack(">H",n)
    else:h+=bytes([0xFF])+struct.pack(">Q",n)
    m=os.urandom(4);s.sendall(h+m+bytes(c^m[i&3] for i,c in enumerate(d)))
def hello(code):return bytes([1])+b"HWVX"+struct.pack("<H",1)+bytes([len(code)])+code
def next_bin(s,stats=None):
    while True:
        op,fin,d=rf(s)
        if op==9: wf(s,10,d); stats is not None and stats.__setitem__('pings',stats.get('pings',0)+1)
        elif op==2:
            if stats is not None and not fin: stats['split']=True
            return d
        elif op==8: raise EOFError("close frame")
def result(name,ok,extra=""): print(("ok  " if ok else "BAD ")+name,extra,flush=True); return ok
allok=True
# 1. plain web request through the tunnel must get a 404, not a hang
try:
    urllib.request.urlopen(urllib.request.Request(url+"/",headers={"User-Agent":"nettest"}),timeout=15,context=ctx); code=200
except urllib.error.HTTPError as e: code=e.code
allok&=result("plain web request answered 404",code==404,f"status {code}")
# 2. wrong room code
s=connect(); wf(s,2,hello(b"WRONGCODE")); d=next_bin(s); allok&=result("wrong room code refused",d==bytes([3,5]),d.hex()); s.close()
# 3. join; echo integrity at sizes that cross WebSocket length forms and proxy buffers
s=connect(); t0=time.time(); wf(s,2,hello(room)); d=next_bin(s)
allok&=result("joined",d[0]==2,f"peer {d[1]} in {(time.time()-t0)*1e3:.0f} ms")
stats={}; sizes=[1,100,125,126,127,1000,8000,16384,16385,30000,59999,60000]; good=True; rtts=[]
for sz in sizes*3:
    body=os.urandom(sz); t=time.time(); wf(s,2,bytes([6])+body); e=next_bin(s,stats); rtts.append((time.time()-t)*1e3)
    good&= e==bytes([6])+body
allok&=result(f"reliable echo intact, {len(sizes)*3} messages 1 B-60 KB",good,f"median {sorted(rtts)[len(rtts)//2]:.0f} ms, proxy split a message: {stats.get('split',False)}")
# 4. latest
wf(s,2,bytes([7,3])+b"snapshot"); e=next_bin(s,stats); allok&=result("latest echo",e==bytes([7,3])+b"snapshot")
# 5. game-rate traffic
n=30*30; lat=[]; s.settimeout(30); nx=time.time(); good=True
for i in range(n):
    t=time.time(); wf(s,2,bytes([7,1])+struct.pack("<d",t)+b"x"*56); e=next_bin(s,stats); lat.append((time.time()-struct.unpack("<d",e[2:10])[0])*1e3); good&=len(e)==66
    nx+=1/30; w=nx-time.time()
    if w>0: time.sleep(w)
lat.sort(); allok&=result("30 s at 30 Hz",good,f"RTT p50 {lat[len(lat)//2]:.0f} p99 {lat[int(len(lat)*.99)]:.0f} max {lat[-1]:.0f} ms")
# 6. no game traffic past Cloudflare's 125 s idle limit: only the host's pings
print("    idling 140 s, answering only the host's pings...",flush=True)
stats['pings']=0; end=time.time()+140; s.settimeout(5); alive=True; err=""
while time.time()<end:
    try:
        op,fin,d=rf(s)
        if op==9: wf(s,10,d); stats['pings']+=1
        elif op==8: alive=False; err="close frame"; break
    except socket.timeout: alive=False; err="no ping for 5 s"; break
    except Exception as e: alive=False; err=repr(e); break
if alive:
    wf(s,2,bytes([6])+b"still here"); s.settimeout(10); e=next_bin(s,stats); alive=e==bytes([6])+b"still here"
allok&=result("survives 140 s without game traffic",alive,f"{stats['pings']} pings answered {err}")
# 7. leaving cleanly
wf(s,8,b""); s.close()
sys.exit(0 if allok else 1)
