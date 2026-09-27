# rtl8_4 as the default DSA tag with PPE hardware flow offload (2026-09-26/27)

Status: DONE and gated. Tree HEAD fafdbd45da (`4a56b47914` default flip,
`10bdec67eb` notifier + review fixes, `fafdbd45da` docs). Image r35533+229
validated on the bench (round 22), adoption branch on François's rebase
validated too (round 23). Follow-up in flight: tagged-WAN offload
(patches 0445 rev + 0446, see the end).

## The problem

The PPE ingress parser classifies past Ethernet plus at most two VLAN
tags whose TPIDs match the VLAN_TPID registers. The Realtek rtl8_4 head
tag (`0x8899 | 0x0400 | 0x0000 | <port>`) sits exactly there, is not a
VLAN tag, so no PPE flow lookup happened on the conduit: hardware
offload only worked with the 802.1Q-based tag_8021q/vsc73xx tagger,
which costs its own compatibility problems (bridge VID on direct TX,
VLAN-aware bridges, `pending-6.18/766`). rtl8_4t (trailer) is
impossible: the RTL8373 register map has no tag-position bit.

## The recipe (all in patch 0445 + rtl837x)

| What | Register | Value under the alias | Reset |
|------|----------|----------------------|-------|
| Ingress TPIDs (IPR) | 0x1e0020 | 0x88990000 (S=0x8899, C=0x0000) | 0x88a88100 |
| Ingress TPIDs (TPR copy) | 0x1d0020 | same | same |
| Conduit port role | 0x1e0000 + 4*port, bit0 | 1 = QinQ core | 0 |
| Egress TPIDs | 0x20040 | S=0x8899, C=0x8100 (0x81008899; was 0x00008899 until 09-27) | 0x810088a8 |
| CPU port egress mode | 0x20020 (port 0) | 0x115: S-VLAN type, both tags TAGGED, VSI mode off | 0x134 |
| Switch CPU-port egress tag mode | RTL8372N 0x6738 bits 7:6 | 01 = KEEP_FORMAT (no VLAN-1 tag after the head) | ORIGINAL |

Why the egress side matters: frames the L2 engine floods to the CPU
(ARP requests, DHCP discover, unknown unicast) pass the egress VLAN
engine, which rebuilds tags from the parsed VIDs with the egress TPIDs.
At reset that produced a lone 0x8100 tag and the tagger dropped it
(dead LAN after a cold boot: DHCP never answered). With the CPU port an
S-VLAN port emitting both tags and the S-TPID aliased, the head tag is
rebuilt intact. Lookup-hit unicast reaches the CPU untouched.

Measured 2026-09-27 (peek module, live registers): UNMODIFIED egress
mode on the CPU port (0x101/0x100) floods nothing, so the rebuild is
mandatory. The tagger never parses the second word of the head, so the
egress C-TPID can stay 0x8100: with EG=0x81008899 ARP 5/5 and DHCP 3/3.
That is what lets a nexthop C-VLAN insert on the WAN keep a real 802.1Q
TPID (tagged-WAN offload, below).

## Boot-order trap and the notifier

netifd opens the conduit before the DSA tree is set up, and DSA links
user ports (CHANGEUPPER on the conduit) before it assigns the conduit's
`dsa_ptr`, so neither ndo_open nor CHANGEUPPER sees a DSA conduit on a
cold boot. The driver re-evaluates the conduit from a netdevice
notifier on a DSA user port's NETDEV_UP (`dsa_port_from_netdev(dev)->
cpu_dp->conduit`), idempotent through a per-port `parser_state`; the
first rtl8_4 conduit owns the chip-wide alias and releases it on close,
tag-protocol change or DSA detach; a second conduit gets -EBUSY.

Never down the conduit over a session that rides it: DSA user ports go
admin-down and do not come back by themselves.

## Validation (rounds 20-23, logs in docs/bench-2026-09-26/)

| Check | Result |
|-------|--------|
| Cold boot after sysupgrade, warm reboot | alias applied by itself, `PPE ingress parser: DSA conduit, rtl8_4 head tag aliased as QinQ` |
| LAN broadcast (ARP), DHCP | 5/5, 3/3 |
| VLAN regression suite (docs/vlan-regression.py) | PASS 4/4 (ours), 3/3 (adoption; different case set) |
| Forged-tag injection | missing: none |
| Masqueraded upload, HW offload | 929-938 Mbit/s, 98.1% idle |
| Download (software by design) | 929 Mbit/s, 83.8-86.0% idle |
| Software path, same flow | 75.4-77.2% idle |
| Tagged WAN (wan.10) | no regression; no hw offload on either tagger (VLAN-push decline) |
| WAN-ingress broadcast with the alias | ARP to the WAN address 10/10 |

## Tagged-WAN offload (in flight 2026-09-27)

ppe_offload declined every tagged-WAN flow with `vlan push to non-vlan
odev=wan`. The flowtable resolves wan.10 to `wan` through
dev_fill_forward_path() and hands the tag as FLOW_ACTION_VLAN_PUSH, so
the "redirect to a VLAN netdevice" branch never ran and the plain-device
branch declined the only shape that occurs. Patch 0446 accepts the push
(nexthop ctag_fmt/cvid already carry it); patch 0445 rev keeps the
egress C-TPID at 0x8100. Bench test needs the dock on the bench WAN as
a VLAN-10 sink (netns v10, 192.168.10.2) and wan.10 on the bench.

## Other state

- François's rebase (kubrickfr, `adopt/flint3-on-upstream` worktree):
  his regression was the dropped pending-6.18/766, not 6.18.52; adoption
  branch = his e1530d21c5 + 766 + rtl837x sync + PPE series + rtl8_4
  default + review fixes (83612219f4), round 23 green. Not pushed, PR
  not opened, security review of his delta parked by the user.
- MLO: our tree loses links on every `wifi reload` (6 GHz first); his
  build rebuilds the link set correctly. `wifi down; wifi up` restores.
- Not in hardware yet: WAN-to-LAN (reply) direction, DNAT, IPv6, Wi-Fi
  ingress; guest-zone src_l3_if collapse is a documented limitation.
