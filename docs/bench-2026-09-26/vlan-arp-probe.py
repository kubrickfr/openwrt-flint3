#!/usr/bin/env python3
# Send 802.1Q-tagged ARP requests (VID given) from IFACE and count tagged replies. No root: run with python3-netraw.
import socket, struct, sys, time, subprocess
iface, vid, src_ip, target = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]; n = 5
s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(3)); s.bind((iface, 0)); s.settimeout(0.4)
mac = bytes.fromhex(open(f"/sys/class/net/{iface}/address").read().strip().replace(':',''))
sip, tip = socket.inet_aton(src_ip), socket.inet_aton(target); got = 0
for i in range(n):
    pkt = b'\xff'*6 + mac + b'\x81\x00' + struct.pack('!H', vid) + b'\x08\x06' + struct.pack('!HHBBH', 1, 0x0800, 6, 4, 1) + mac + sip + b'\x00'*6 + tip
    s.send(pkt); t = time.time()
    while time.time() - t < 0.4:
        try: f = s.recv(1600)
        except socket.timeout: break
        if f[12:14] == b'\x81\x00' and (struct.unpack('!H', f[14:16])[0] & 0xfff) == vid and f[16:18] == b'\x08\x06' and struct.unpack('!H', f[24:26])[0] == 2 and f[32:36] == tip:
            got += 1; break
    time.sleep(0.2)
print(f"tagged ARP vid {vid} for {target}: {got}/{n} replies")
