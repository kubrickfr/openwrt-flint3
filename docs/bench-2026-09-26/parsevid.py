#!/usr/bin/env python3
# Parse a raw pcap from the DSA conduit: VLAN VID distribution of frames carrying TCP port 5201, by direction (CPU->port = src MAC of conduit).
import struct, sys, collections
d = open(sys.argv[1], 'rb').read(); conduit = bytes.fromhex(sys.argv[2].replace(':',''))
magic = struct.unpack('<I', d[:4])[0]; e = '<' if magic in (0xA1B2C3D4, 0xA1B23C4D) else '>'
off = 24; st = collections.Counter()
while off + 16 <= len(d):
    _, _, incl, _ = struct.unpack(e + 'IIII', d[off:off+16]); off += 16; f = d[off:off+incl]; off += incl
    if len(f) < 38: continue
    src = f[6:12]; et = struct.unpack('!H', f[12:14])[0]; o = 14; vid = None; pcp = None
    if et == 0x8100: tci = struct.unpack('!H', f[14:16])[0]; vid = tci & 0xfff; pcp = tci >> 13; et = struct.unpack('!H', f[16:18])[0]; o = 18
    if et != 0x0800 or f[o+9] != 6: continue
    ihl = (f[o] & 0xf) * 4; sp, dp = struct.unpack('!HH', f[o+ihl:o+ihl+4])
    if 5201 not in (sp, dp): continue
    tot = struct.unpack('!H', f[o+2:o+4])[0]
    st[('CPU->port' if src == conduit else 'port->CPU', 'vid=%s' % vid, 'pcp=%s' % pcp, 'big' if tot > 100 else 'small')] += 1
for k, v in sorted(st.items(), key=lambda x: -x[1])[:12]: print("    %5d  %s" % (v, ' '.join(k)))
