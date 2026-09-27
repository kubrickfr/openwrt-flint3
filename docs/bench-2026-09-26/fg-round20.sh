#!/bin/bash
# Round 20: cold boot on the driver default (rtl8_4) must apply the alias by itself (user-port NETDEV_UP path).
D=/home/percy/dev/flint3-port/docs/bench-2026-09-26
OURS=/home/percy/dev/flint3-port/upstream/openwrt/bin/targets/qualcommbe/ipq53xx/openwrt-qualcommbe-ipq53xx-glinet_gl-be9300-squashfs-sysupgrade.bin
SSHO="-o ConnectTimeout=6 -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null"
S4(){ timeout ${T:-60} ssh $SSHO root@192.168.1.1 "$@" 2>/dev/null; }
waitback(){ sleep 60; for i in $(seq 1 36); do sleep 10; nmcli con up "Wired connection 2" >/dev/null 2>&1; R=$(T=8 S4 'grep REVISION /etc/openwrt_release'); [ -n "$R" ] && { echo "  back after ~$((60+i*10))s: $R"; return 0; }; done; echo "  !! no ssh after 7 min"; return 1; }
regs(){ timeout 30 scp $SSHO -O $D/peek/peek.ko root@192.168.1.1:/tmp/ >/dev/null 2>&1; S4 'insmod /tmp/peek.ko 2>/dev/null; p(){ echo "r $1" > /sys/kernel/debug/peek; cat /sys/kernel/debug/peek | cut -d" " -f2; }; echo "  tagger=$(cat /sys/class/net/lan/dsa/tagging) want=$(cat /etc/tagproto.want 2>/dev/null || echo none)  regs: IPR=$(p 0x3a1e0020) TPR=$(p 0x3a1d0020) role1=$(p 0x3a1e0004) EG=$(p 0x3a020040) eg0=$(p 0x3a020020) sw6738=$(echo "r 0x6738" > /sys/kernel/debug/90000.mdio-1:1d/reg; cat /sys/kernel/debug/90000.mdio-1:1d/reg | grep -oE "0x[0-9a-f]+$")"; dmesg | grep -iE "PPE ingress parser|reprogram the PPE|EBUSY" | sed -E "s/^\[[ 0-9.]+\] /  /" | head -4'; }
echo "== round 20 $(date +%H:%M:%S): flash (want file stays absent)"; S4 'rm -f /etc/tagproto.want /etc/tagproto.pending; sync'; timeout 180 scp $SSHO -O "$OURS" root@192.168.1.1:/tmp/img.bin >/dev/null 2>&1 || { echo "  !! scp failed"; exit 1; }
S4 "sysupgrade -T /tmp/img.bin >/dev/null 2>&1 && { (sleep 1; sysupgrade -v /tmp/img.bin) >/tmp/su.log 2>&1 & echo '  sysupgrade launched'; }"; waitback || exit 1; sleep 30; nmcli con up "Wired connection 2" >/dev/null 2>&1; sleep 8; echo "  village v4: $(ip -4 -o addr show enp66s0 | awk '{print $4}')"
echo "--- cold boot #1 (post-sysupgrade) ---"; regs
echo "--- LAN broadcast / VLAN suite ---"; ~/.config/bin/python3-netraw $D/wan-arp-test.py enp66s0 192.168.1.1 5 | sed 's/^/  /'; cd /home/percy/dev/flint3-port && timeout 900 docs/vlan-regression.py --ap 192.168.1.1 --inj local --inj-iface enp66s0 --port lan4 2>&1 | grep -E "OVERALL"
echo "== warm reboot (same default) =="; S4 '(sleep 1; reboot) >/dev/null 2>&1 &'; waitback || exit 1; sleep 30; nmcli con up "Wired connection 2" >/dev/null 2>&1; sleep 8; echo "  village v4: $(ip -4 -o addr show enp66s0 | awk '{print $4}')"
echo "--- boot #2 (warm) ---"; regs
echo "ROUND20 DONE $(date +%H:%M:%S)"
