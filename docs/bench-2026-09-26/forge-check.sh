#!/bin/bash
# Run the forged-frame check on the bench under rtl8_4 (+alias): capture on the conduit AND on lan4, inject, classify.
D=/home/percy/dev/flint3-port/docs/bench-2026-09-26
S4(){ timeout ${T:-60} ssh -o ConnectTimeout=6 -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@192.168.1.1 "$@" 2>/dev/null; }
echo "tagger: $(S4 'cat /sys/class/net/lan/dsa/tagging')  vlan_filtering: $(S4 'cat /sys/class/net/br-lan/bridge/vlan_filtering')"
S4 'rm -f /tmp/cd.pcap /tmp/l4.pcap; (timeout 8 tcpdump -ni lan -s 128 -w /tmp/cd.pcap >/dev/null 2>&1 &); (timeout 8 tcpdump -ni lan4 -s 128 -w /tmp/l4.pcap >/dev/null 2>&1 &)'; sleep 1
~/.config/bin/python3-netraw $D/forge-inject.py enp66s0 5; sleep 8
for f in cd l4; do timeout 40 scp -o BatchMode=yes -O root@192.168.1.1:/tmp/$f.pcap /tmp/forge-$f.pcap >/dev/null 2>&1; done
python3 - /tmp/forge-cd.pcap /tmp/forge-l4.pcap <<'PY'
import struct,sys,collections
kinds={'020000000002':'ctrl','020000000010':'pri-tag','020000000011':'88a8-tag','020000000012':'forged-8899'}
for name,path in (("conduit lan",sys.argv[1]),("user port lan4",sys.argv[2])):
    try: d=open(path,'rb').read(); assert len(d)>24
    except Exception: print(f"  {name}: (no capture)"); continue
    e='<' if struct.unpack('<I',d[:4])[0] in (0xA1B2C3D4,0xA1B23C4D) else '>'; off=24; st=collections.Counter()
    while off+16<=len(d):
        _,_,incl,_=struct.unpack(e+'IIII',d[off:off+16]); off+=16; f=d[off:off+incl]; off+=incl
        k=kinds.get(f[6:12].hex());
        if not k: continue
        st[(k,'as: '+f[12:24].hex())]+=1
    print(f"  {name}:"); [print(f"     {v:3d} {k[0]:<12} {k[1]}") for k,v in sorted(st.items())]
    seen={k[0] for k in st}; print("     missing:", sorted(set(kinds.values())-seen) or "none")
PY
