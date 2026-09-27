#!/bin/bash
S=/tmp/claude-1000/-home-percy-dev-flint3-port/c53ce6e2-25da-4996-8e7e-c5d0bc3dd9b9/scratchpad
S4(){ timeout ${T:-40} ssh -o ConnectTimeout=6 -o BatchMode=yes root@192.168.1.1 "$@" 2>/dev/null; }
sw(){ S4 "echo '$1 $2 $3' > /sys/kernel/debug/90000.mdio-1:1d/reg; cat /sys/kernel/debug/90000.mdio-1:1d/reg | tr -d '\n'"; echo; }
pk(){ S4 "echo 'r $1' > /sys/kernel/debug/peek; cat /sys/kernel/debug/peek" | tr -d '\n'; echo; }
pw(){ S4 "echo 'w $1 $2' > /sys/kernel/debug/peek; cat /sys/kernel/debug/peek" | tr -d '\n'; echo; }
cap(){ S4 'rm -f /tmp/c.pcap; (timeout 6 tcpdump -ni lan -s 64 -w /tmp/c.pcap >/dev/null 2>&1 &)'; sleep 1; timeout 8 iperf3 -c 10.20.30.13 --bind-dev enp66s0 -t 2 -f m -R >/dev/null 2>&1; sleep 4; timeout 40 scp -o BatchMode=yes -O root@192.168.1.1:/tmp/c.pcap $S/c.pcap >/dev/null 2>&1; python3 $S/parsetag.py $S/c.pcap 94:83:c4:ba:26:02 | grep -E "sw->CPU" | head -2; }
cpu(){ B=$(S4 'head -1 /proc/stat'); R=$(timeout 30 iperf3 -c 10.20.30.13 --bind-dev enp66s0 -t 10 -f m 2>&1 | awk '/receiver/{print $7" "$8} /iperf3:/{print "STALL"}' | head -1); A=$(S4 'head -1 /proc/stat')
  python3 - "$B" "$A" "$1" "$R" <<'PY'
import sys
try:
    b=[int(x) for x in sys.argv[1].split()[1:]]; a=[int(x) for x in sys.argv[2].split()[1:]]; d=[x-y for x,y in zip(a,b)]; t=sum(d) or 1
    print(f"  [{sys.argv[3]:<36}] {sys.argv[4]:<16} idle {(d[3]+d[4])/t*100:5.1f}%  softirq {d[6]/t*100:5.1f}%")
except Exception as e: print(f"  [{sys.argv[3]}] no CPU sample ({e}); iperf: {sys.argv[4]}")
PY
}
echo "== 1. switch: port3 egress tag mode -> KEEP_FORMAT (0x6738 bits7:6 = 01) =="; sw w 0x6738 0x40; sleep 1; echo -n "  LAN ping: "; ping -c2 -W2 -q 192.168.1.1 | grep -oE "[0-9]+% packet loss"; echo "  conduit sw->CPU frames now:"; cap
echo "== 2. baseline CPU with switch poke only (rtl8_4, hw offload) =="; cpu "rtl8_4 / HW / switch-untagged only"
echo "== 3. PPE: VLAN_TPID (IPR 0x3a1e0020, TPR 0x3a1d0020) = stag 0x8899 | ctag 0x0000; port1 role=core =="
pw 0x3a1e0020 0x88990000; pw 0x3a1d0020 0x88990000; pw 0x3a1e0004 0x1; sleep 1
echo -n "  LAN ping: "; ping -c2 -W2 -q 192.168.1.1 | grep -oE "[0-9]+% packet loss"; echo -n "  through: "; ping -c2 -W2 -q -I enp66s0 10.20.30.13 | grep -oE "[0-9]+% packet loss"
S4 'fw4 reload >/dev/null 2>&1'; sleep 2
cpu "rtl8_4 / HW / ALIASED (core)"; cpu "rtl8_4 / HW / ALIASED (core) repeat"
S4 'echo "  iperf flows HW_OFFLOAD: $(grep -c "dport=5201.*HW_OFFLOAD" /proc/net/nf_conntrack)"; dmesg | grep -iE "ppe_offload" | tail -2 | sed -E "s/^\[[ 0-9.]+\] /  /"'
echo "== 3b. WAN egress during an aliased upload: frame lengths / any 0x8899 residue? =="; S4 '(timeout 6 tcpdump -ni wan -e -c 3 "tcp port 5201 and greater 1000" > /tmp/w.txt 2>&1 &)'; sleep 1; timeout 8 iperf3 -c 10.20.30.13 --bind-dev enp66s0 -t 2 -f m >/dev/null 2>&1; sleep 3; S4 'grep -E "^[0-9]" /tmp/w.txt | head -3 | cut -c1-130'
echo "== 4. variant: port1 role=edge (TPIDs kept) =="; pw 0x3a1e0004 0x0 >/dev/null; sleep 1; cpu "rtl8_4 / HW / aliased, role=edge"
echo "== 5. variant: EG_VLAN_TPID too (0x3a020040 = stpid 0x8899 | ctpid 0x0000 -> 0x00008899), role=core =="; pw 0x3a1e0004 0x1 >/dev/null; pw 0x3a020040 0x00008899 >/dev/null; sleep 1; echo -n "  through: "; ping -c2 -W2 -q -I enp66s0 10.20.30.13 | grep -oE "[0-9]+% packet loss"; cpu "rtl8_4 / HW / aliased + eg tpid"
echo "== restore =="; pw 0x3a020040 0x810088a8 >/dev/null; pw 0x3a1e0020 0x88a88100 >/dev/null; pw 0x3a1d0020 0x88a88100 >/dev/null; pw 0x3a1e0004 0x0 >/dev/null; sw w 0x6738 0x0 >/dev/null; sleep 1; cpu "rtl8_4 / HW / restored"
echo "ALIAS2 DONE"
