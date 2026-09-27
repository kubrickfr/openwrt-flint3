#!/usr/bin/env python3
# Send ARP requests for the bench WAN IP from village's house-LAN NIC (raw socket) and count replies.
# Tests WAN-ingress *broadcast* handling on the bench (flooded to its CPU) -- run with python3-netraw.
import socket, struct, sys, time, fcntl
iface, target = sys.argv[1], sys.argv[2]; n = int(sys.argv[3]) if len(sys.argv) > 3 else 5
s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(0x0806)); s.bind((iface, 0)); s.settimeout(0.5)
mac = open(f"/sys/class/net/{iface}/address").read().strip()
src_mac = bytes.fromhex(mac.replace(':','')); src_ip = socket.inet_aton(sys.argv[4] if len(sys.argv) > 4 else [l.split()[3].split("/")[0] for l in __import__("subprocess").run(["ip","-4","-o","addr","show",iface],capture_output=True,text=True).stdout.splitlines()][0])
dst_ip = socket.inet_aton(target); replies = 0
for i in range(n):
    pkt = b'\xff'*6 + src_mac + b'\x08\x06' + struct.pack('!HHBBH', 1, 0x0800, 6, 4, 1) + src_mac + src_ip + b'\x00'*6 + dst_ip
    s.send(pkt); t = time.time()
    while time.time() - t < 0.5:
        try: f = s.recv(1500)
        except socket.timeout: break
        if f[12:14] == b'\x08\x06' and struct.unpack('!H', f[20:22])[0] == 2 and f[28:32] == dst_ip:
            replies += 1; break
    time.sleep(0.2)
print(f"ARP {target} via {iface}: {replies}/{n} replies")
