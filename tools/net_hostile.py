#!/usr/bin/env python3
"""Attacks a running nettest host with protocol violations. Each attack must get
closed or refused; afterwards the host must still serve a normal client.

  nettest host 47815 ROOM 99 60 &       # 99: stays up when attackers leave
  tools/net_hostile.py 47815 ROOM
  nettest client 127.0.0.1:47815 ROOM 200 25

Run the host under sanitizers, at -O2 too: see HWVX_ASAN."""
import base64, os, socket, struct, sys, time
port=int(sys.argv[1]); room=sys.argv[2].encode()
def conn():
    s=socket.create_connection(("127.0.0.1",port),timeout=8); return s
def http(s):
    key=base64.b64encode(os.urandom(16)).decode()
    s.sendall(f"GET /hwvx HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n".encode())
    b=b""
    while b"\r\n\r\n" not in b:
        c=s.recv(1)
        if not c: raise EOFError
        b+=c
    assert b" 101 " in b.split(b"\r\n")[0],b
def frame(op,d,fin=True,mask=True,declared=None):
    n=len(d) if declared is None else declared
    h=bytes([(0x80 if fin else 0)|op]); mb=0x80 if mask else 0
    if n<126:h+=bytes([mb|n])
    elif n<65536:h+=bytes([mb|126])+struct.pack(">H",n)
    else:h+=bytes([mb|127])+struct.pack(">Q",n)
    if mask:
        m=os.urandom(4);h+=m;d=bytes(c^m[i&3] for i,c in enumerate(d))
    return h+d
def hello(code=room,proto=1,magic=b"HWVX"):
    return bytes([1])+magic+struct.pack("<H",proto)+bytes([len(code)])+code
def closed(s,t=8):
    "True once the host closes; replies before that are read and ignored."
    s.settimeout(t); end=time.time()+t; got=b""
    try:
        while time.time()<end:
            c=s.recv(65536)
            if not c: return True,got
            got+=c
    except socket.timeout: return False,got
    except OSError: return True,got
    return False,got
results=[]
def attack(name,fn):
    try:
        ok=fn(); results.append((name,ok))
    except Exception as e: results.append((name,f"exception {e!r}"))
def a_garbage():
    s=conn(); s.sendall(os.urandom(4096)); return closed(s)[0]
def a_long_head():
    s=conn()
    try:
        s.sendall(b"GET /hwvx HTTP/1.1\r\nX: "+b"a"*20000)
    except OSError: return True
    return closed(s)[0]
def a_browser():
    s=conn(); s.sendall(b"GET / HTTP/1.1\r\nHost: x\r\n\r\n"); c,g=closed(s); return c and b"404" in g
def a_silent():
    s=conn(); return closed(s,8)[0]     # handshake deadline: 5 s
def a_no_hello():
    s=conn(); http(s); s.sendall(frame(2,bytes([6])+b"data before hello")); return closed(s)[0]
def a_unmasked():
    s=conn(); http(s); s.sendall(frame(2,hello(),mask=False)); return closed(s)[0]
def a_oversize():
    s=conn(); http(s); s.sendall(frame(2,b"",declared=2**40)); return closed(s)[0]
def a_oversize2():
    s=conn(); http(s); s.sendall(frame(2,b"",declared=60003)); return closed(s)[0]
def a_bad_magic():
    s=conn(); http(s); s.sendall(frame(2,hello(magic=b"XXXX"))); return closed(s)[0]
def a_hello_lies():
    s=conn(); http(s); s.sendall(frame(2,bytes([1])+b"HWVX"+struct.pack("<H",1)+bytes([200])+b"short")); return closed(s)[0]
def a_empty():
    s=conn(); http(s); s.sendall(frame(2,b"")); return closed(s)[0]
def a_text():
    s=conn(); http(s); s.sendall(frame(1,b"hello")); return closed(s)[0]
def a_old_version():
    s=conn(); http(s); s.sendall(frame(2,hello(proto=9))); c,g=closed(s); return c and g[2:]==bytes([3,4]) or (c, g)
def a_wrong_code():
    s=conn(); http(s); s.sendall(frame(2,hello(code=b"NOPE"))); return closed(s)[0]
def a_frag_flood():
    s=conn(); http(s); s.sendall(frame(2,hello()))
    try:
        for i in range(10): s.sendall(frame(i and 0 or 2,b"z"*20000,fin=False))
    except OSError: return True
    return closed(s)[0]
def a_bad_key():
    s=conn(); http(s); s.sendall(frame(2,hello())); s.sendall(frame(2,bytes([7,200])+b"x")); return closed(s)[0]
def a_client_notice():
    s=conn(); http(s); s.sendall(frame(2,hello())); s.sendall(frame(2,bytes([5,1,0]))); return closed(s)[0]
def a_second_hello():
    s=conn(); http(s); s.sendall(frame(2,hello())); s.sendall(frame(2,hello())); return closed(s)[0]
def a_ping_no_read():
    # never reads a pong: host must stay healthy and eventually drop or carry on
    s=conn(); http(s); s.sendall(frame(2,hello())); s.settimeout(0.5)
    p=frame(9,b"x"*125)
    try:
        for i in range(40000): s.sendall(p)
    except OSError: return True
    s.close(); return True
for f in (a_garbage,a_long_head,a_browser,a_silent,a_no_hello,a_unmasked,a_oversize,a_oversize2,a_bad_magic,a_hello_lies,a_empty,a_text,a_old_version,a_wrong_code,a_frag_flood,a_bad_key,a_client_notice,a_second_hello,a_ping_no_read):
    attack(f.__name__,f)
bad=0
for n,ok in results:
    print(f"{'ok  ' if ok is True else 'BAD '} {n} {'' if ok is True else ok}")
    bad+=ok is not True
sys.exit(bad!=0)
