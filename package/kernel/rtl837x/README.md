# RTL8372N DSA driver candidate

This package contains an early, RTL8372N-only DSA candidate for OpenWrt: the
P0 bring-up described below plus, on this branch, hardware offload of one
bridge, VLAN-unaware or VLAN-aware, which has not run on hardware yet. See
[PROVENANCE.md](PROVENANCE.md) for source and register origins. The detailed
[first-hardware test plan](FIRST-HARDWARE-TEST.md) covers the build gate, the
bench order, Flint 3 tests, expected results, failure evidence, and a maintainer report template.
The [pre-PR review and remediation plan](PRE-PR-PLAN.md) records blocking
source findings, build requirements and the staged publication gates.
The [P1 feasibility and implementation plan](P1-RESEARCH.md) maps public
VLAN/L2/STP sources, conflicting field descriptions, dependencies and future
acceptance tests. This branch implements its P1-A to P1-D steps (no MDB)
together, without a runtime switch; its section 1 records that deviation.

## P0 scope

The current code provides:

- MDIO-backed 32-bit register access, chip identification, and software reset.
- Serialized Clause 45 (MMD) access to the integrated PHYs on ports 4–7. They
  are registered as C45 devices (ID 0x001ccad0 in MMDs 1, 3, 7 and 31) and
  are driven by the kernel's Realtek PHY driver ("RTL8224 2.5Gbps PHY"), the
  same binding the SDK-based driver gets on this board. That driver provides
  2.5G autonegotiation, status decoding and `ethtool --cable-test`.
- Phylink plumbing for internal PHY ports 4–7 and the 10GBASE-R SerDes MACs
  on ports 3 and 8.
- SerDes mode selection and the optional `sds0/1-{rx,tx}-swap` device-tree
  properties, using the small set of open register operations listed in the
  provenance manifest.
- Native RTL8_4 CPU tagging, a VLAN 1/PVID 1 bootstrap, and CPU-only port
  isolation/flooding with learning disabled for standalone ports. P0 had no
  hardware bridge offload and ran every bridge in software. Release-7 testing
  passed bidirectional traffic across all six LAN pairs with 30-second iperf3
  runs and 20/20 pings. Release 8 passed all 33 setup readbacks and partially
  verified CPU-only isolation for one standalone port.
- A shared indirect-table engine (`rtl837x_table.c`) with VLAN and L2 unicast
  entry codecs, serialised by one table mutex. Setup resets the VLAN table
  (VIDs 2, 4094 and 4095 must then read empty, else every VID from 2 up is
  written empty), pins the VLAN classification controls (priority tags to the
  PVID, VID 4095 never admitted, no per-VID learning disable, no port-based
  FID), writes VID 0 (empty) and VLAN 1 with a readback, and clears the L2
  table once learning is off. The DSA FDB add/delete/dump callbacks use its
  L2 primitives: static entries, CPU-port entries allowed, and DSA VIDs 0
  and 1 sharing the entry {MAC, VLAN 1}, which is kept while either is held.
  A per-port flush of learned entries backs fast-age. They act on the
  offloaded bridge below.
- A setup step that only offload needs and that has never run on hardware
  (the VLAN table reset, VID 0, a VLAN 1 readback mismatch, an L2 clear
  timeout) does not fail setup: it logs `hardware bridging disabled:
  <reason>`, and every bridge then stays in software as in P0. Any other
  setup error still fails setup.
- Control-frame and lookup-miss setup, the prerequisite for bridge offload:
  01:80:c2:00:00:00 and 02–0f (BPDU, LACP, EAPOL, LLDP) are trapped to the
  CPU through the reserved-multicast engine instead of being forwarded,
  lookup misses and unmatched source addresses forward per the flood masks,
  a lookup hit outside the ingress port's isolation mask is handled as
  unknown, the learn-limit action forwards, no frame leaves by the port it
  came in on, ASIC IGMP/MLD snooping is off, and the STP state of every
  available port is FORWARDING. Not yet hardware-verified; see the CF checks in
  [FIRST-HARDWARE-TEST.md](FIRST-HARDWARE-TEST.md).
- DSA STP, fast-age and ageing callbacks. Standalone ports are FORWARDING
  when up and DISABLED when down; bridged ports follow the bridge. See the L
  checks.
- Hardware offload of one bridge: bridge join links the port, through port
  isolation, with the members that are in the same VLAN mode (CPU-only for
  every other port, and for a member while its `vlan_filtering` mode
  changes), and the bridge flags `learning`, `flood`, `mcast_flood` and
  `bcast_flood` drive the learning limit and the per-port flood-mask bits
  while a port is bridged. Leave restores CPU-only isolation, no flooding, no
  learning, flushes the port's learned entries and sets FORWARDING; a port
  whose isolation could not be restored is held in STP DISABLED until a
  later write succeeds. DSA installs the addresses the software bridge learns
  on foreign ports (Wi-Fi) as static CPU-port entries (assisted learning).
  Not yet hardware-verified; see the B, AL and U checks.
- VLAN-aware bridges (`vlan_filtering 1`, netifd `bridge-vlan` sections) are
  offloaded too (`rtl8372n_vlan.c`): bridge VLANs become VLAN table entries
  (tagged/untagged members), a port of a filtering bridge gets ingress
  filtering, the bridge PVID (tagged-only admission and PVID 0 without one)
  and untagging per VLAN, and `vlan_filtering` can be toggled at runtime.
  The CPU port is a member of the bridge's own VLANs (`br-lan.X`) and of
  those of foreign ports such as Wi-Fi, and receives frames as they were on
  the wire. Leaving the bridge, or turning filtering off, restores the
  VLAN-unaware state with PVID 1 and VLAN 1 membership. Not yet
  hardware-verified; see the VA checks.

The driver currently supports one CPU port and rejects cascaded DSA ports.
Only RTL8372N is accepted by chip-ID detection. The candidate matches only
`realtek,rtl8372n`; the BE9300 candidate DTS uses that explicit compatible.
Legacy `realtek,rtl837x` nodes, including the current BE6500 node, are not
supported by this P0 replacement. Keep the shipping package on the maintainer's
branch until board compatibility and feature deferrals have been agreed.

## Not in P0

Not implemented: MDB (the switch floods multicast to every member with
`mcast_flood`, whatever IGMP snooping decides), MST, LAG and rate limiting
(Issues #47 and #49), hardware MIB/ethtool counters, a GPIO controller, and
EEE (kept off on the internal PHYs). Bridge port flags other than learning
and flooding (`hairpin`, `isolated`, `locked`, `mab`, `mcast_to_unicast`) are
refused on ports of the offloaded bridge; a port bridged in software accepts
them. LAN-to-LAN traffic of the offloaded bridge bypasses nftables bridge
rules.

Limitations of the offload (none hardware-verified yet):

- It is unconditional: no module parameter or build switch. To keep a bridge
  in software, give each LAN port an 8021q upper before it joins ("software
  mode", [FIRST-HARDWARE-TEST.md](FIRST-HARDWARE-TEST.md) section 4).
- One hardware bridge. DSA gives it to the first bridge that enslaves a LAN
  port and never moves it: if another bridge (say one holding lan4) gets a
  LAN port first, at boot or while br-lan is down after an `ifdown lan`,
  br-lan runs in software until both are rebuilt in order. The other bridge
  forwards through the CPU (B4).
- 8021q uppers are refused (EBUSY) on ports of the offloaded bridge, and a
  port that already has one is bridged in software: the switch cannot keep
  an upper's VID out of the bridge. A netifd `lanN.M` device next to a br-lan
  holding lanN thus depends on bring-up order; use bridge VLANs instead (U
  checks).
- On a port of an offloaded VLAN-aware bridge, DSA programs an upper's VID
  before it refuses the upper (8021q adds the VID first; DSA core). If the
  VID is one of the port's bridge VLANs, the failed attempt drops the port's
  PVID/untagged flags and then the port itself from that VLAN in the switch,
  while `bridge vlan show` still lists it. Delete and re-add the VLAN on the
  port to repair it (U4).
- Inside an offloaded VLAN-unaware bridge tagged frames are switched as they
  are, so unicast tagged with a VID other than 1 towards the router (a
  `br-lan.10` on that bridge) or anything else behind the CPU misses the
  table, whose host entries are in VLAN 1, and floods to the bridge's ports
  with `flood` on. Use a VLAN-aware br-lan (`bridge-vlan` sections) for VLANs
  that end on the router (B2).
- A VLAN-aware port without a bridge PVID admits tagged frames only
  (TAG_ONLY, never run on this chip). If that admission acts before the
  link-local trap, untagged BPDUs, LLDP and EAPOL from such a port never
  reach the CPU (VA9).
- Trapped link-local frames need the `tag_rtl8_4` kernel patch named under
  "Build output": without it a bridge with STP off stops relaying BPDUs and
  `group_fwd_mask` frames between LAN ports (CF5).

Standalone ports and ports of a VLAN-unaware bridge are VLAN-unaware, as DSA
expects: 802.1Q frames of any VID should reach the CPU with their tag intact
and CPU frames leave unmodified, so 8021q uppers on standalone ports work,
switched by the CPU. This is not yet hardware-verified; see the V checks in
[FIRST-HARDWARE-TEST.md](FIRST-HARDWARE-TEST.md). Tagged traffic to and from
the router stays on the CPU path (PPE flow offload declines VLANs on DSA user
ports and ports of a VLAN-aware bridge). VID 1 and priority-tagged frames now
also reach Linux tagged; a host that tags VID 1 needs a VLAN-filtering bridge
with VID 1 untagged/PVID, or a `lanN.1` upper on a port outside the
offloaded bridge. With no per-bridge FDB, standalone ports share VLAN 1 with
the bridge (their frames to a bridge host's MAC should be handled as
unknown and reach the CPU only, B3), and VLAN 1 is never removed from the
CPU. Untagged router frames towards a port of a VLAN-aware bridge rely on the
CPU tag's KEEP bit overriding the port's untag rules (VA4).

The SerDes path selects the 10GBASE-R mode and applies the board's optional
polarity swaps. It does not contain PHY firmware, vendor patch arrays, or the
full SerDes initialization sequence used by vendor SDKs. The initial diagnostic
run failed T1 because the in-tree RTL8224 PHY driver bound first; release 6
fixed the binding. Release 7's [ARM64 module CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37454484695)
and [full-image CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37454704524)
passed. The hardware report confirms all four private PHY bindings, stable
three-warm/one-cold reset behavior, and no port 0–2 power-down warnings. T3's
follow-up supplies the missing LAN1 link event and confirms T3 physical cycling
on every jack. Release 7 also passes T2 with all four ports attached to
`br-lan`, and T4 with 100/100 pings and 0% loss. T5 passed all six
software-bridge pairs; counters record 77 new LAN3 RX drops and TCP
retransmissions, without a defined throughput threshold. Release 8 adds T1
and partial T6 evidence on tested source commit `7e51247b`: setup readbacks passed
33/33, and one standalone port did not forward the tested traffic to one bridge
observer. The reverse-direction counter check lacks a packet capture; broader
isolation and reserved control-frame behavior remain open. The `lan` conduit `tx_errors` value still reads
`2^64-1`; the raw PPE MIB operands needed to interpret it are unavailable. The
WAN-side `10GBASE-R link not up before USXG_EN` message still appears, while
boot continues and the switch CPU link is up. See
[PHY-PROBE-REPORT.md](PHY-PROBE-REPORT.md) for the full evidence and remaining
P0 gates.

The setup-readback follow-up covers the VLAN 1 table entry, available PVIDs,
isolation masks, learning limits, CPU flood destinations and VLAN filter
controls. Focused ARM64 `W=1` module CI passed for source commit `7e51247b`.
Release 8 booted an image from that exact commit and reports 33/33 readbacks
passing. Its one-port negative-forwarding result is partial T6 evidence, not a
general VLAN/isolation sign-off. The broad OpenWrt PR matrix is not green; see
[BUILD-REPORT.md](BUILD-REPORT.md).

## Device tree

The current Flint 3 node uses `compatible = "realtek,rtl8372n"`, `reg = <29>`,
CPU port 3, and user ports 4–7. The driver consumes these top-level boolean
properties when present:

```dts
sds0-rx-swap;
sds0-tx-swap;
sds1-rx-swap;
sds1-tx-swap;
```

The P0 driver does not provide a GPIO controller and ignores the legacy
`rtl837x,sds0mode`, MDI-reverse, and PHY-TX-polarity properties. SerDes mode is
selected by phylink from the port's `phy-mode`; only `10gbase-r` is accepted
for ports 3 and 8. The driver powers the internal PHYs down across reset and
teardown only; phylib owns their power state (PMA low power, mirrored at OCP
`0xa610`) while they are attached, as under the SDK-based driver.

## Build output

The OpenWrt package remains `kmod-rtl837x-dsa`. It builds
`rtl8372n_dsa.ko` and enables the kernel's `tag_rtl8_4` DSA tagger. It
depends on MDIO devres and on `kmod-phy-realtek` for the internal PHYs. Linux 6.18 is the
supported API; 6.12 compatibility is not claimed.

Bridge offload also needs the target kernel patch
`target/linux/qualcommbe/patches-6.18/0457-net-dsa-tag_rtl8_4-do-not-mark-trapped-link-local-frames-as-forwarded.patch`:
the tagger then never sets `offload_fwd_mark` on a link-local frame
(01:80:c2:00:00:0x), as `tag_brcm` does, because this driver traps those
frames and the switch never forwards them. Without it, a bridge with STP off
stops relaying BPDUs between LAN ports and loops through two external
switches go undetected. The patch is only right with a driver that traps
these frames: with the SDK-based `rtl837x` driver, which leaves them to the
hardware, such a bridge would send each one twice.

See [P0-STATUS.md](P0-STATUS.md) and [BUILD-REPORT.md](BUILD-REPORT.md) for
the current evidence and remaining gates. The ARM64 module build and full
BE9300 AP-config OpenWrt package/DTB/image build passed for source revision
`954a84bd7c46dbbb2412eeddfb300aad8b4cff35` ([module run](https://github.com/MNeroba/openwrt-flint3/actions/runs/37059225390),
[target image run](https://github.com/MNeroba/openwrt-flint3/actions/runs/37059229177)).
Diagnostic `1b7a32bef2` passed [module CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37273928197)
and [image CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37273960255),
but T1 failed at PHY binding. Release 5 failed module compilation because it
referenced a private kernel macro. Release 6 [module CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37397943227)
and [image CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37397974073)
passed; its candidate module SHA-256 is
`9ccd428ae58f7650d8f7e47455c24250349e840758208e800146663a44037263`.
The maintainer's [release-6 report](https://github.com/perceival/openwrt-flint3/pull/104#issuecomment-6014637917)
passes T0/T1/T2/T4/T7. The [T3/T5 follow-up](https://github.com/perceival/openwrt-flint3/pull/104#issuecomment-6016542658)
adds partial release-6 jack and pair coverage.
Release 7's [module CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37454484695)
and [full-image CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37454704524)
passed, and the maintainer's [hardware report](https://github.com/perceival/openwrt-flint3/pull/104#issuecomment-6037094988)
confirms T0/T1/T7 and the full six-pair T5 matrix. The [follow-up](https://github.com/perceival/openwrt-flint3/pull/104#issuecomment-6044579250)
confirms T2/T3/T4 on release 7, including physical link events on all four
jacks and a 100-packet ping with 0% loss. Release 8 adds partial T6 evidence.

## Staged follow-up

1. Extend the Release-8 T6 result: capture the reverse direction directly,
   test other standalone-port/observer combinations that are feasible, and
   record reserved multicast/BPDU behavior without creating a physical loop.
2. Run the bench phases of [FIRST-HARDWARE-TEST.md](FIRST-HARDWARE-TEST.md)
   section 4 in order: setup (VLAN table reset, VID 0/VLAN 1, L2 clear, CF
   traps) with a software-mode bridge, the VLAN-unaware port checks, the
   offloaded VLAN-unaware bridge (CF5, B, L, AL and U checks), then the
   VLAN-aware one (VA checks). A failed phase keeps the previous phase's
   configuration.
3. Resolve the remaining VLAN/L2 field meanings and the SDK-only rows of
   [PROVENANCE.md](PROVENANCE.md), following
   [P1-RESEARCH.md](P1-RESEARCH.md); then add MDB management.
4. Reach P1 parity or record agreed deferrals before porting LAG (#47) and
   rate limiting (#49). Restore other baseline interfaces in separate, sourced
   and tested changes or obtain explicit maintainer agreement to defer them.
