#!/bin/bash
S4(){ timeout ${T:-90} ssh -o ConnectTimeout=6 -o BatchMode=yes root@192.168.1.1 "$@" 2>/dev/null; }
apwait(){ # poll up to 100 s for hostapd MLD to settle; print links + errors
  for i in $(seq 1 20); do sleep 5; N=$(S4 'hostapd_cli -i ap-mld0 status 2>/dev/null | grep -E "^num_links" | cut -d= -f2'); [ -n "$N" ] && [ "$N" = "$1" ] && break; done
  S4 'echo "  AP after wait: num_links=$(hostapd_cli -i ap-mld0 status 2>/dev/null | grep -E "^num_links" | cut -d= -f2) links: $(iw dev ap-mld0 info 2>/dev/null | grep -oE "channel [0-9]+ \([0-9]+ MHz\)" | tr "\n" " ")  radios-up: $(wifi status 2>/dev/null | grep -cE "\"up\": true")"; logread | grep -iE "hostapd|ath12k|mld|netifd: radio" | grep -iE "fail|error|could not|invalid|CAC|DFS|disabled|link" | tail -5 | sed -E "s/^.*(hostapd|kernel|netifd)/  \1/" | cut -c1-160'; }
client(){ nmcli con up bench-mlo >/dev/null 2>&1; sleep 12; C=$(nmcli -t -f GENERAL.CONNECTION dev show wlp67s0f0 2>/dev/null | cut -d: -f2); L=$(iw dev wlp67s0f0 link 2>/dev/null | grep -oE "freq: [0-9.]+" | tr "\n" " "); V=$(ip -4 -o addr show wlp67s0f0 | awk '{print $4}')
  if [ "$C" != "bench-mlo" ]; then echo "  client: NOT on bench-mlo (on '${C:-none}') -> no association"; nmcli dev disconnect wlp67s0f0 >/dev/null 2>&1; return; fi
  echo "  client: on bench-mlo links: ${L:-none}  v4: $V"; timeout 25 iperf3 -c 10.20.30.13 --bind-dev wlp67s0f0 -t 5 -f m -R 2>&1 | awk '/receiver/{print "  down: "$7" "$8} /iperf3:/{print "  down: STALL"}' | head -1; }
setr(){ S4 "uci set wireless.radio0.disabled='$1'; uci set wireless.radio1.disabled='$2'; uci set wireless.radio2.disabled='$3'; uci commit wireless; logread -c >/dev/null 2>&1; wifi reload >/dev/null 2>&1"; }
echo "== T0 all three (reference) =="; setr 0 0 0; apwait 3; client
echo "== T1 5 GHz OFF -> expect 2.4 + 6 =="; setr 0 1 0; apwait 2; client
echo "== T2 2.4 + 5 GHz OFF -> expect 6 only =="; setr 1 1 0; apwait 1; client
echo "== T3 6 GHz OFF -> expect 2.4 + 5 =="; setr 0 0 1; apwait 2; client
echo "== T4 2.4 OFF -> expect 5 + 6 =="; setr 1 0 0; apwait 2; client
echo "== T5 all back -> expect 3 =="; setr 0 0 0; apwait 3; client
echo "== T6 runtime: 'wifi down radio1' while associated with 3 links =="; S4 'logread -c >/dev/null; wifi down radio1'; sleep 20; S4 'echo "  AP: num_links=$(hostapd_cli -i ap-mld0 status 2>/dev/null | grep -E "^num_links" | cut -d= -f2) links: $(iw dev ap-mld0 info 2>/dev/null | grep -oE "channel [0-9]+ \([0-9]+ MHz\)" | tr "\n" " ")"'; echo "  client still: $(nmcli -t -f GENERAL.CONNECTION dev show wlp67s0f0 | cut -d: -f2) links: $(iw dev wlp67s0f0 link 2>/dev/null | grep -oE 'freq: [0-9.]+' | tr '\n' ' ')"; timeout 25 iperf3 -c 10.20.30.13 --bind-dev wlp67s0f0 -t 5 -f m -R 2>&1 | awk '/receiver/{print "  down: "$7" "$8} /iperf3:/{print "  down: STALL"}' | head -1
echo "== T7 'wifi up radio1' -> expect 3 again =="; S4 'wifi up radio1'; apwait 3; client
echo "== T8 runtime: deny client on link1 (5 GHz) + deauth, with a healthy 3-link AP =="
MAC=$(S4 'iw dev ap-mld0 station dump | grep -oE "^Station [0-9a-f:]+" | awk "{print \$2}" | head -1'); echo "  sta: $MAC"
S4 "logread -c >/dev/null; hostapd_cli -i ap-mld0_link1 deny_acl ADD_MAC $MAC 2>/dev/null || hostapd_cli -i ap-mld0 deny_acl ADD_MAC $MAC; hostapd_cli -i ap-mld0 deauthenticate $MAC >/dev/null 2>&1"; sleep 4; nmcli con up bench-mlo >/dev/null 2>&1; sleep 15
echo "  client after: $(nmcli -t -f GENERAL.CONNECTION dev show wlp67s0f0 | cut -d: -f2) state=$(nmcli -t -f DEVICE,STATE dev status | grep '^wlp67s0f0' | cut -d: -f2) links: $(iw dev wlp67s0f0 link 2>/dev/null | grep -oE 'freq: [0-9.]+' | tr '\n' ' ')"
S4 "logread | grep -iE 'hostapd.*(ap-mld0|link)' | grep -iE 'auth|assoc|denied|acl|disconnect|timeout' | tail -6 | sed -E 's/^.*hostapd: /  /' | cut -c1-150; hostapd_cli -i ap-mld0_link1 deny_acl DEL_MAC $MAC >/dev/null 2>&1; hostapd_cli -i ap-mld0 deny_acl DEL_MAC $MAC >/dev/null 2>&1; hostapd_cli -i ap-mld0 deny_acl CLEAR >/dev/null 2>&1"
echo "== T9 ACL cleared: client reconnect =="; client
echo "MLO2 DONE $(date +%H:%M:%S)"
