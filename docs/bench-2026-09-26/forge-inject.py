#!/usr/bin/env python3
# Forged-frame injector for the rtl8_4+alias security check. Sends from a user port:
#   pri  : 802.1Q priority-tagged (VID 0)          -> must arrive as untagged data on the injecting port's PVID
#   88a8 : 0x88a8 S-tag (VID 7) + IP-looking payload -> must NOT be parsed as a CPU tag / cross VLANs
#   8899 : forged Realtek head tag claiming source port 1, then 0x0800 -> must be treated as data, not a tag
#   ctrl : untagged control frame (liveness)
# Each frame carries a distinctive source MAC per kind so the capture side can attribute it.
import socket, struct, sys, time
iface = sys.argv[1]; count = int(sys.argv[2]) if len(sys.argv) > 2 else 5
s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW); s.bind((iface, 0))
DST = b'\xff' * 6; PAY = b'\x45\x00\x00\x1c' + b'\x00' * 24 + b'FORGE-TEST'
kinds = {
 'ctrl': bytes.fromhex('020000000002') , 'pri': bytes.fromhex('020000000010'), '88a8': bytes.fromhex('020000000011'), '8899': bytes.fromhex('020000000012') }
frames = {
 'ctrl': DST + kinds['ctrl'] + b'\x88\xb5' + PAY,
 'pri':  DST + kinds['pri']  + b'\x81\x00' + struct.pack('!H', 0x0000) + b'\x08\x00' + PAY,
 '88a8': DST + kinds['88a8'] + b'\x88\xa8' + struct.pack('!H', 7) + b'\x08\x00' + PAY,
 '8899': DST + kinds['8899'] + b'\x88\x99' + bytes.fromhex('0400') + bytes.fromhex('0000') + bytes.fromhex('0001') + b'\x08\x00' + PAY,
}
for i in range(count):
    for k, f in frames.items(): s.send(f)
    time.sleep(0.05)
print("sent", count, "x", ",".join(frames))
