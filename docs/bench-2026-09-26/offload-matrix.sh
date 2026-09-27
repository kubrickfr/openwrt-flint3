#!/bin/bash
# Pure-masquerade LAN->WAN / WAN->LAN CPU-cost matrix on the bench, sink = pve iperf3.
SINK=10.20.30.13; BENCH=192.168.1.1
SSH="ssh -o ConnectTimeout=6 -o BatchMode=yes -o StrictHostKeyChecking=no root@$BENCH"
snap(){ timeout 12 $SSH 'head -1 /proc/stat; cat /sys/class/net/lan/dsa/tagging' 2>/dev/null; }
run(){ # $1 label, $2 iperf extra args
  B=$(snap)
  timeout 45 iperf3 -c $SINK --bind-dev enp66s0 -t 15 -f m $2 > /tmp/ip.$$ 2>&1 &
  sleep 8; CT=$(timeout 10 $SSH 'grep "dport=5201" /proc/net/nf_conntrack | grep -c HW_OFFLOAD' 2>/dev/null)
  wait; R=$(grep -E "receiver" /tmp/ip.$$ | awk '{print $7" "$8}'); grep -qE "error|unable|refused" /tmp/ip.$$ && R="FAILED: $(grep -m1 -E 'error|unable|refused' /tmp/ip.$$)"
  A=$(snap)
  python3 - "$B" "$A" "$1" "$R" "$CT" <<'PY'
import sys
def parse(s):
    l=s.strip().splitlines(); f=[int(x) for x in l[0].split()[1:]]; return f,l[1].strip()
b,bt=parse(sys.argv[1]); a,at=parse(sys.argv[2]); d=[x-y for x,y in zip(a,b)]; tot=sum(d)
print(f"  [{sys.argv[3]:<32}] tagger={at:<14} iperf-flows-HW_OFFLOAD={sys.argv[5]:>2}  {sys.argv[4]:<16} idle {(d[3]+d[4])/tot*100:5.1f}%  softirq {d[6]/tot*100:5.1f}%", flush=True)
PY
}
setoff(){ timeout 40 $SSH "uci set firewall.@defaults[0].flow_offloading=$1; uci set firewall.@defaults[0].flow_offloading_hw=$2; uci commit firewall; fw4 reload >/dev/null 2>&1; sleep 2; echo \"fw=$1 hw=$2 flowtable-offload-flag=\$(nft list ruleset | grep -c 'flags offload')\"" 2>/dev/null; }
reboot_into(){ timeout 20 $SSH "echo $1 > /etc/tagproto.want; rm -f /etc/tagproto.pending; (sleep 1; reboot) >/dev/null 2>&1 &" >/dev/null 2>&1
  for i in $(seq 1 15); do sleep 12; T=$(snap | tail -1); [ "$T" = "$1" ] && { echo "  bench back on $1 after ~$((i*12))s"; sleep 20; return 0; }; done; echo "  !! bench did not come back on $1"; return 1; }
matrix(){ # $1 tagger label
  echo "== $1: $(setoff 1 1)"; run "$1 / HW           / LAN->WAN up" ""; run "$1 / HW           / WAN->LAN down" "-R"
  echo "== $1: $(setoff 1 0)"; run "$1 / SW-flowtable / LAN->WAN up" ""; run "$1 / SW-flowtable / WAN->LAN down" "-R"
  echo "== $1: $(setoff 0 0)"; run "$1 / none         / LAN->WAN up" ""; run "$1 / none         / WAN->LAN down" "-R"
  echo "== restore: $(setoff 1 1)"
}
echo "waiting for $SINK:5201 ..."
for i in $(seq 1 360); do timeout 3 nc -z -w2 $SINK 5201 && break; sleep 5; done
timeout 3 nc -z -w2 $SINK 5201 || { echo "sink never opened"; exit 1; }
echo "sink open at $(date +%H:%M:%S)"
T=$(snap | tail -1); echo "bench tagger now: $T"
if [ "$T" = "rtl8_4" ]; then matrix rtl8_4; reboot_into vsc73xx-8021q && matrix tag_8021q; reboot_into rtl8_4
else matrix tag_8021q; reboot_into rtl8_4 && matrix rtl8_4; fi
echo "MATRIX DONE $(date +%H:%M:%S)"
