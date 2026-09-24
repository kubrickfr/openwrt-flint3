# Sorting out rtl837x per-port identity: tag protocol options

## The problem, precisely

Per-port RX **identity** (counters, bridge FDB, anything keyed on source port) is
wrong on rtl837x user ports whenever they are bridged. Routed traffic was also
broken; that part is fixed (patch 766 + link-local carve-out, verified on ap2).
What remains is identity only.

Two collapse mechanisms, both measured:

- **VLAN-unaware bridge:** `dsa_tag_8021q_bridge_join()` replaces each port's
  unique standalone VID with a shared bridge VID (3088 = VBID 1). RX falls back
  to `dsa_tag_8021q_find_port_by_vbid()` -> first bridged port with carrier.
- **VLAN-aware bridge** (what the APs run): `rtl837x_commit_pvid()` sets the port
  PVID to the *bridge* VID (10), overwriting the tag_8021q RX VID. RX falls back
  to `dsa_find_designated_bridge_port_by_vid()` -> lowest chip port with carrier.

This is not an rtl837x defect. tag_8021q has exactly one field -- the VLAN ID --
and it must carry either source-port identity or bridge VLAN semantics. When a
bridge owns the VLAN, identity has nowhere to go. Upstream vsc73xx does the same.

The whole thing traces to one line in `rtl837x_get_tag_protocol()`: we return
`DSA_TAG_PROTO_VSC73XX_8021Q` rather than the chip's native tag because "the
proprietary 0x8899 tag defeats the IPQ5332 EDMA checksum parser".

## Option B -- QinQ / outer-tag identity: REJECTED on security grounds

Idea: port identity in an outer S-tag, bridge VLAN in the inner C-tag. The EDMA
RX descriptor does report SVLAN and CVLAN separately (`edma_rx.h` word6/word7),
and the vendor API ships `svlan.c`, so it was not obviously impossible.

Rejected, and the reason is blast radius rather than "QinQ is insecure":

- The classic double-encapsulation VLAN-hopping attack does **not** apply -- the
  tag would be switch-inserted, CPU-port-only, and never leaves the board.
- But it puts identity in **the same field the bridge uses for VLAN membership**.
  A forged or mis-parsed tag there aliases VLAN membership -- what the firewall
  zones are built on. With a distinct-ethertype CPU tag a forged tag aliases only
  the *source port*. Same bug class, very different consequence.
- Our preconditions make it worse: on the APs (`vlan_filtering=1`) user ports run
  `ACCEPT_FRAME_TYPE_ALL`, and `rtl837x_dsa_ops.c:917-921` documents a still-open
  injection path -- a client injecting a frame tagged with the bridge VID it is
  legitimately a member of reaches the CPU via the VBID path. QinQ identity would
  be built directly on that open edge.
- Tag depth would become a contract between four independent parsers (RTL8372N,
  PPE/EDMA, DSA tagger, nftables). Every identity bug in this project so far has
  been a parsing-contract disagreement.

## Option C -- retune the CPU tag ethertype: RULED OUT

`rtk_cpuTag_tpid_set()` exists (`RTL8373_CPU_TAG_TPID_CTRL`), default 0x8899, so
the TPID really is programmable. But the Realtek tag is **8 bytes** and a VLAN tag
is 4: pointing the parser at it with TPID 0x8100 makes it land 4 bytes early.
Changing the ethertype cannot fix a length mismatch.

## Option D -- rtl8_4t trailer tag: RULED OUT BY THE CHIP

The kernel supports it (`DSA_TAG_PROTO_RTL8_4T`, `tag_rtl8_4.c`): same 8-byte tag
placed between payload and CRC, so all headers stay in their normal positions and
a conduit that cannot parse past a header tag still classifies the frame.

Not usable here: the RTL8373 CPU-tag block has **no position control**. The whole
register set is `CPU_TAG_TPID_CTRL`, `CPU_TAG_AWARE_CTRL` (per-port mask),
`CPU_TAG_CTRL` (INT/EXT enable + insert mode ALL/TRAPPING/NONE). Insert mode is
*when* to insert, not *where*. The tag is always after the source MAC.

Note also the kernel's own warning: the trailer "will break most checksums,
either in software or hardware", and its TX path software-checksums to compensate.

## Option A -- native rtl8_4 header tag: LEADING, but blocked on one measurement

Gives precise identity in every regime and removes the tag_8021q overload
entirely -- which also closes the `:917-921` injection edge, since that path only
exists because identity is derived from the VID.

**The open question, and it is decisive.** The driver comment says 0x8899 defeats
the *checksum* parser, but that parser is the PPE ingress parser, which also does
L3/L4 classification for **flow lookup**, RSS hashing, and the TCP-flag
exceptions. The argument "checksum offload matters less now that PPE offload
carries the bulk" is **inverted** if the tag change stops the PPE from parsing
LAN-ingress frames at all: LAN->WAN flows enter through the tagged conduit, so
no flow entry would match and the measured 937 Mbit/s at 96% CPU idle would be
gone. On the APs the equivalent cost is RSS -- unparseable frames likely hash to
one queue, putting all wired RX on one core next to ath12k.

Verified de-risk: the checksum failure mode is *safe*, not corrupting --
`edma_rx.c:141-150` gates on the parser's `pid` and falls through to
`CHECKSUM_NONE`, so the stack verifies in software.

Forgery question, and it has an answer: `RTL8373_CPU_TAG_AWARE_CTRL` carries a
**per-port mask**. Only the CPU port should be tag-aware, so an 0x8899 frame
forged from a user port is treated as data, not as a CPU tag. Confirm the driver
sets that mask before committing.

## Next step

Implement DSA's `.change_tag_protocol` op (plus the `dsa-tag-protocol` DT
property) rather than a module parameter. That gives runtime switching via
`/sys/class/net/lan/dsa/tagging` with user ports down, allows A/B on the bench
unit without reflashing, and is the form upstream would accept.

Then measure on the bench router, flipped to `rtl8_4`:

1. does a TCP flow still reach `[HW_OFFLOAD]`?
2. does a router-side `tcpdump` still show only the 3-way handshake?
3. is RX still spread across queues (per-queue IRQ counts)?
4. does `cpu_code` still show the TCP-flag exceptions firing?

**Decision rule:** if the parser breaks on 0x8899, A is viable per role at best --
the APs could take it if the RSS/CPU cost measures acceptable, but the router must
stay on a tag the PPE can parse.

## Scope honesty

Leaving tag_8021q makes much of the 2026-09-23/24 tagger work moot *for that
configuration*: patch 766, the link-local carve-out, the tag_8021q VLAN add/del
guards, and part of the ingress-policy change. The VLAN regression suite would
need re-running under the new tagger. Not a reason to avoid the change, but it is
not a one-line change either.


## A/B round 1 (2026-09-24): harness works, result is CONFOUNDED

`.change_tag_protocol` is implemented in `rtl837x_dsa_ops.c`
(`rtl837x_tag_protocol_apply()` / `_unapply()`), both taggers are built by the
package, and the runtime switch works:

    # echo rtl8_4 > /sys/class/net/lan/dsa/tagging     -> write_rc=0
    # cat /sys/class/net/lan/dsa/tagging               -> rtl8_4

Preconditions, learned the hard way: `dsa_tree_change_tag_proto()` requires
**every user port AND the conduit** to be `!IFF_UP` (`net/dsa/dsa.c:987-1002`).
Bringing only lan1-4 down gives `-EBUSY`; `lan` must go down too. That drops the
box's own LAN, so the switch has to run detached.

**Baseline (vsc73xx-8021q), bench router, 1 Gbit client -> ap2:**
937 Mbit/s, 1 `[HW_OFFLOAD]` flow, `cpucode:47`+`88` bump on close. RX lands on a
single queue (`rxdesc_15`) — expected for a single flow, so RSS spread needs
parallel flows to measure and was NOT measured this round.

**rtl8_4: LAN dead.** Switch succeeded and links came back
(`lan`/`lan1`/`lan4` UP, conduit re-negotiated 10Gbps), but:

    conduit lan rx: 278 packets / 59054 bytes in ~25 s   (essentially nothing)
    ip neigh on br-lan:  EMPTY
    ping 192.168.1.100:  100% loss

**Do NOT read this as "rtl8_4 defeats the PPE parser".** The test is confounded:
`rtl837x_tag_protocol_unapply()` does not unwind tag_8021q's switch state, and
our own `rtl837x_tag_8021q_vlan_del()` guard (added 2026-09-23 to keep the
link-local carve-out's VLANs alive while bridged) actively blocks part of the
unwind — visible in dmesg as

    port 4 failed to notify tag_8021q VLAN 3076 deletion: -ENOENT
    (same for 3077/3078/3079)

So the switch was left holding tag_8021q's standalone VIDs and port PVIDs **while
also** inserting the 0x8899 CPU tag. Frames were doubly mis-described; the PPE
parser is only one of several candidate causes.

**The safety net worked, twice.** Because the tagging selection is not
persistent, "reboot on loss of the LAN client" is a guaranteed recovery, and the
box came back on the default tagger both times with PPE offload healthy.

### To make round 2 a clean test

`rtl837x_tag_protocol_unapply()` must leave the switch in a sane VLAN state when
leaving tag_8021q:

1. allow the standalone-VID deletion during a tagger change (the guard should
   key on "still bridged AND still using tag_8021q", not on bridged alone);
2. reset user-port PVIDs so the switch is not still applying tag_8021q VLANs;
3. only then enable the 0x8899 CPU tag.

Only after that does a dead LAN actually implicate the parser.


## A/B round 2 (2026-09-24): the unwind was the bug, and rtl8_4 breaks TCP specifically

Round 2 added a real unwind: `rtl837x_tag_protocol_unapply()` now clears
tag_8021q's PVID bookkeeping and calls `rtl837x_seed_vlan_table()` to restore the
base layout (VLAN 1, all ports member, PVID 1), and a new
`gsw->tag_proto_changing` flag lets `rtl837x_tag_8021q_vlan_del()` drop the
standalone VIDs it otherwise deliberately keeps for the link-local carve-out.

With that, the switch to `rtl8_4` no longer kills the box outright -- and the
remaining failure is far more specific and far more informative:

| probe | result |
|---|---|
| ICMP to the router, 56 B | **0% loss, 0.28 ms** |
| ICMP to the router, **1400 B** | **0% loss** |
| TCP to the router :22 / :80 / :443 | **all time out** |
| TCP **between two LAN hosts** (hardware-switched) | **works** |

So the L2/L3 path is completely intact in both directions, including large
frames. Only TCP *terminating on or forwarded by the router CPU* is broken, while
TCP that the switch forwards in hardware -- never seeing the CPU tag -- is fine.

**That is a checksum-offload failure, not a parser-classification failure.** ICMP
is checksummed by the kernel in software and survives; TCP checksums are offloaded
to hardware, and the 8-byte 0x8899 tag sitting between the source MAC and the
ethertype shifts the L4 offset, so the EDMA computes/validates over the wrong
range.

This finally makes the driver's original one-line comment concrete: 0x8899 does
defeat the parser, and the cost lands squarely on **hardware checksum offload**,
in both directions, not merely on RX classification.

### What option A actually requires

Not just "flip the tagger". When `rtl8_4` is active the driver must also drop
checksum offload on the affected path -- clear `NETIF_F_IP_CSUM`/`IPV6_CSUM` (TX)
and `NETIF_F_RXCSUM` on the conduit and user ports, so the stack checksums in
software. That is the real, measured price of precise per-port identity.

Whether PPE **flow offload** also dies is still unmeasured -- the box became
unreachable before that could be tested. It remains the open question, and it is
the one that decides whether A is viable for the router role at all.

### My watchdog was wrong, and the lesson generalises

The safety net probed liveness with `ping`. ICMP is exactly what survives this
failure, so the watchdog concluded "LAN OK after switch" and never rebooted --
leaving the unit reachable by ping but with no TCP, i.e. no way back in.

**A liveness probe must exercise the same transport you need in order to
recover.** For an SSH-managed box that means a TCP connect to port 22, not a
ping. The earlier rounds only recovered because the failure was total.


## Rounds 3-4 (2026-09-24): the blocker is in the EDMA driver, not the switch

**Watchdog fix validated.** Probing liveness over TCP (`nc <client> 22`, expect an
SSH banner) instead of ICMP caught the failure immediately and auto-recovered the
box by reboot -- twice, no hands needed. The ping-based version had required a
physical power cycle.

**Trying to disable checksum offload from the switch driver does not work.**
`rtl837x_conduit_csum_offload()` clears the conduit's csum/TSO bits inside
`.change_tag_protocol` and the log confirms it runs:

    rtl837x-dsa ...: disabled checksum offload on conduit lan for tagger change

...but `ethtool -k lan` still showed everything on. `edma_port_open()`
(`edma_port.c:109-112`) re-ORs `EDMA_NETDEV_FEATURES` into `features`,
`hw_features`, `vlan_features` **and** `wanted_features` on *every* open -- and a
tagger switch necessarily bounces the conduit right afterwards, undoing it.

**And it cannot be disabled from userspace either.** After the port is up:

    ethtool -K lan rx off            -> rx-checksumming: off      (works)
    ethtool -K lan tso off gso off   -> off                       (works)
    ethtool -K lan tx off            -> tx-checksumming: STILL ON
    ethtool -K lan tx-checksum-ip-generic off
        -> tx-checksum-ip-generic: off
           tx-checksum-ipv4:   on [fixed]     <-- kernel substitutes these
           tx-checksum-ipv6:   on [fixed]
           tx-checksumming:    on

So TX checksum offload stays on no matter what, and TCP stays broken.

### Conclusion: option A is blocked on a change to the EDMA/PPE driver

Not the switch driver. `edma_tx.c:441-445` sets the descriptor's `IP_CSUM` and
`L4_CSUM` bits whenever `skb->ip_summed == CHECKSUM_PARTIAL` and never passes
`skb->csum_start`/`csum_offset` -- it leaves hardware to find L3/L4 by parsing.
Any DSA **header** tag moves those headers and the offload then checksums the
wrong range.

Three ways out, in increasing order of value:

1. Skip the offload bits when the netdev is a DSA conduit whose tagger inserts a
   header tag (`netdev_uses_dsa()` plus the tagger's `needed_headroom`), so the
   stack checksums in software. Smallest, and makes rtl8_4 usable.
2. Pass `skb->csum_start`/`csum_offset` into the TX descriptor so hardware does
   not re-parse at all. Better -- keeps offload working *with* a header tag.
3. Stop `edma_port_open()` re-asserting `wanted_features` on every open, which is
   what makes the feature unmanageable from outside in the first place.

Until one of those lands, **rtl8_4 cannot be evaluated on this hardware**, and
the question that actually decides the architecture -- does PPE *flow* offload
survive the 0x8899 tag -- stays unanswered, because the box is unreachable over
TCP long before a flow can be set up.
