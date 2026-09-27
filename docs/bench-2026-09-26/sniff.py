#!/usr/bin/env python3
# Raw sniffer: frames on IFACE involving TCP port 5201; reports ethertype/VLAN, sizes, TCP flags, checksum validity.
import socket, struct, sys, time, collections
iface, secs = sys.argv[1], float(sys.argv[2])
s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(3)); s.bind((iface, 0)); s.settimeout(0.5)
def csum(b):
    if len(b) % 2: b += b'\0'
    t = sum(struct.unpack('!%dH' % (len(b)//2), b)); t = (t >> 16) + (t & 0xffff); t += t >> 16; return (~t) & 0xffff
stats = collections.Counter(); samples = []
end = time.time() + secs
while time.time() < end:
    try: f = s.recv(65535)
    except socket.timeout: continue
    if len(f) < 34: continue
    dst, src, et = f[:6], f[6:12], struct.unpack('!H', f[12:14])[0]; off = 14; vid = None
    if et == 0x8100: vid = struct.unpack('!H', f[14:16])[0] & 0xfff; et = struct.unpack('!H', f[16:18])[0]; off = 18
    if et != 0x0800: continue
    ihl = (f[off] & 0xf) * 4; tot = struct.unpack('!H', f[off+2:off+4])[0]; proto = f[off+9]
    if proto != 6: continue
    ip = f[off:off+ihl]; sp, dp = struct.unpack('!HH', f[off+ihl:off+ihl+4])
    if 5201 not in (sp, dp): continue
    tcp = f[off+ihl:off+tot]; flags = tcp[13]
    pseudo = ip[12:16] + ip[16:20] + b'\0\x06' + struct.pack('!H', len(tcp))
    ok = csum(pseudo + tcp) == 0
    dirn = 'IN ' if dst == bytes.fromhex(open('/sys/class/net/%s/address' % iface).read().strip().replace(':','')) else 'OUT'
    key = (dirn, 'vlan%s' % vid if vid is not None else 'untagged', 'tcpcsum_ok' if ok else 'TCPCSUM_BAD', 'ipcsum_ok' if csum(ip) == 0 else 'IPCSUM_BAD', 'len%s' % ('<=100' if tot <= 100 else '>100'), 'flags=%02x' % flags)
    stats[key] += 1
    if len(samples) < 6 and dirn == 'IN ' and tot > 100: samples.append((tot, flags, vid, ok))
print("sniff %s %.0fs:" % (iface, secs))
for k, v in sorted(stats.items(), key=lambda x: -x[1])[:16]: print("  %5d  %s" % (v, ' '.join(k)))
print("  big-IN samples (len,flags,vid,csum_ok):", samples)
