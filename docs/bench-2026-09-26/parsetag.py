#!/usr/bin/env python3
# Distribution of the 8-byte 0x8899 tag words on a raw conduit capture, by direction.
import struct, sys, collections
d=open(sys.argv[1],'rb').read(); conduit=bytes.fromhex(sys.argv[2].replace(':',''))
magic=struct.unpack('<I',d[:4])[0]; e='<' if magic in (0xA1B2C3D4,0xA1B23C4D) else '>'
off=24; st=collections.Counter(); n=0
while off+16<=len(d):
    _,_,incl,_=struct.unpack(e+'IIII',d[off:off+16]); off+=16; f=d[off:off+incl]; off+=incl; n+=1
    if len(f)<22 or f[12:14]!=b'\x88\x99': continue
    w1,w2,w3=struct.unpack('!HHH',f[14:20]); inner=struct.unpack('!H',f[20:22])[0]
    st[('CPU->sw' if f[6:12]==conduit else 'sw->CPU','w1=%04x'%w1,'w2=%04x'%w2,'w3=%04x'%w3,'inner=%04x'%inner)]+=1
print("  frames:",n)
for k,v in sorted(st.items(),key=lambda x:-x[1])[:14]: print("   %6d  %s"%(v,' '.join(k)))
