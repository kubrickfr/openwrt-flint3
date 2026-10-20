# RTL8372N P0 first-hardware test plan

**Status (2026-10-08):** Release 7 (`79afa2c51a3c2396c33ed511ed092d799c52e1bf`)
passed T0/T1/T2/T3/T4/T5/T7. Release 8 tested source commit
`7e51247b3882567ce891481395b34e2a2c25f116` on BE9300. Its
[report](https://github.com/perceival/openwrt-flint3/pull/104#issuecomment-6048213461)
and [raw logs](https://gist.github.com/perceival/3172215cb83c6ea3866fd55bee0808be)
show T1 boot/link evidence and 33/33 expected setup readbacks. The standalone
`lan2` test found no E8450-sourced ARP or unicast frames at one bridge observer;
reverse-direction evidence is counter-only. T6 is **PARTIAL**, not a general
VLAN/isolation sign-off. Reserved control-frame behavior and remaining
recovery/concurrency checks are open. See
[P0-STATUS.md](P0-STATUS.md),
[PHY-PROBE-REPORT.md](PHY-PROBE-REPORT.md), and [BUILD-REPORT.md](BUILD-REPORT.md)
for evidence and remaining gates. Start each run only after T0 passes for the
exact image revision under test.

The setup-readback follow-up covers VLAN 1, available PVIDs, port isolation,
learning limits, flood masks and VLAN-filter controls. It uses the driver's
register transport; the VLAN-table command reads the table and does not write
it. Focused module CI passed for tested source commit `7e51247b`. Release 8 then
reported 33/33 matching readbacks and a one-port negative-forwarding test on
that exact source commit. Extend T6 with direct reverse-direction capture and broader
isolation/control-frame cases; do not treat the existing result as general
VLAN offload qualification.

This plan is intended for the driver maintainer and the Flint 3 owner running
the test. It is not a claim that any step has passed.

The candidate scope and sources are documented in [README.md](README.md) and
[PROVENANCE.md](PROVENANCE.md). The board description is
[`ipq5332-gl-be9300.dts`](../../../target/linux/qualcommbe/dts/ipq5332-gl-be9300.dts).

## 1. What this first test should prove

The first run is a narrow smoke test of the P0 path:

1. The MDIO-attached switch is detected at address 29 and the driver accepts
   the RTL8372N chip ID.
2. The switch resets and the minimum register setup completes without MDIO,
   regmap, SDS-command, or DSA registration errors.
3. DSA creates the CPU link and four user ports with the Flint 3 jack mapping.
4. The four board SerDes polarity properties are applied and the CPU link
   carries traffic after the driver's software reset.
5. Internal PHY ports bind to `RTL8372N internal PHY (P0)`, report link
   changes and carry untagged traffic to the router. In P0 LAN-to-LAN traffic
   ran through the software bridge and CPU; this build offloads one bridge,
   VLAN-unaware or VLAN-aware, to the switch (see the B and VA checks).
6. These results survive warm reboot and a cold power cycle.

The run does **not** qualify general VLAN offload, FDB/MDB offload, STP, LAG,
rate limiting, GPIO, EEE, or performance parity. Do not use tests for those
features as P0 acceptance criteria.

The future P1 acceptance matrix is in [P1-RESEARCH.md](P1-RESEARCH.md). Run it
only against a revision that implements the corresponding P1 step, after P0
passes. In particular, establish CPU-only BPDU reception without a loop before
a future hardware-STP loop test; the current P0 does not implement STP offload.

## 2. Safety and test setup

Use a spare GL-BE9300 and establish the project's documented recovery path
with a known-good image before the test. The AP config documents no usable
normal serial console on this board: do not assume UART access. Use the known
TFTP/initramfs recovery path and `ramoops`/`/sys/fs/pstore` crash evidence. If a
working console or independent management path is available, keep it
connected. Follow the backup and installation
instructions in the [top-level README](../../../README.md); do not test this
candidate on the only router you can recover remotely. The P0 code issues a
switch software reset during probe, and full post-reset PHY/SerDes initialization
has not yet been demonstrated.

Prepare:

- One Flint 3 with the candidate image built from the exact source revision
  under test.
- Tested recovery/initramfs access, a known-good image and persistent crash
  log collection. Record any actually usable console or independent management
  path; access through the candidate's LAN ports alone is insufficient.
- Two independent Ethernet hosts. For link-rate checks, use a peer and cable
  that support 2.5 Gb/s; a 1 Gb/s peer is still useful for basic link and
  forwarding checks.
- `ip`, `bridge`, `ping`, and `ethtool` on the router where available;
  `iperf3` on both external hosts for optional throughput measurements.
- A way to identify which physical LAN jack is connected to each host.

Do not connect the two hosts through an unmanaged switch for a LAN-to-LAN
forwarding test: it may forward frames locally without exercising the Flint 3.
Connect each host directly to a different Flint 3 LAN jack.

## 3. Device-tree configuration to use

For the first run, retain the current board topology and all four polarity
flags. The P0 driver consumes `compatible = "realtek,rtl8372n"`, MDIO address
29, port 3 as the 10GBASE-R CPU port, and the boolean properties shown below.
It does not implement the switch GPIO controller or the legacy
`rtl837x,sds0mode` property; those declarations have been removed from the
Flint 3 candidate DTS so that the example matches the driver.

```dts
rtl837x: rtl837x-dsa@29 {
	compatible = "realtek,rtl8372n";
	reg = <29>;
	#address-cells = <1>;
	#size-cells = <0>;

	sds0-rx-swap;
	sds0-tx-swap;
	sds1-rx-swap;
	sds1-tx-swap;

	ports {
		#address-cells = <1>;
		#size-cells = <0>;

		port@3 {
			reg = <3>;
			label = "cpu";
			ethernet = <&xgmac1>;
			phy-mode = "10gbase-r";
			fixed-link {
				speed = <10000>;
				full-duplex;
			};
		};

		/* Front-panel mapping: LAN1=7, LAN2=6, LAN3=5, LAN4=4. */
		port@4 { reg = <4>; label = "lan4"; phy-mode = "internal"; };
		port@5 { reg = <5>; label = "lan3"; phy-mode = "internal"; };
		port@6 { reg = <6>; label = "lan2"; phy-mode = "internal"; };
		port@7 { reg = <7>; label = "lan1"; phy-mode = "internal"; };
	};
};
```

The board DTS records that polarity swaps were required on the previous
working driver. Keep all four enabled for the candidate's baseline run; the
new driver resets the switch before applying them, so the old result must be
reconfirmed after reset. Do not start by sweeping polarity combinations.

## 4. Build and identify the exact candidate

Build on a supported Linux OpenWrt build host. The current checkout's macOS
host is not a successful build environment for this package.

```sh
cp package/kernel/rtl837x/P0-FEEDS.conf feeds.conf
./scripts/feeds update -a
./scripts/feeds install -a
cp configs/ap.config .config
make defconfig
make package/kernel/rtl837x/clean
make package/kernel/rtl837x/compile V=s
make -j"$(nproc)"
```

Before the hardware run, record the exact source revision used for the
image. A commit hash alone is insufficient if the build includes uncommitted
changes; attach the corresponding complete patch or commit the candidate first.
Record the installed feed revisions and image checksum on the build host:

```sh
git rev-parse HEAD
./scripts/feeds list -s -f > p0-feeds.conf
sha256sum bin/targets/qualcommbe/ipq53xx/*glinet_gl-be9300*sysupgrade.bin
find bin/packages -type f \( -name '*rtl837x-dsa*.apk' -o -name '*rtl837x-dsa*.ipk' \) -print
```

The build gate is successful only if the package and full target image finish
without errors, and the image contains `rtl8372n_dsa.ko` plus the kernel's
`tag_rtl8_4` module. Keep the build log with the test report. Install only by
the project's established recovery/install procedure on a recoverable test
unit; this document deliberately gives no unverified flash command.

Before installation, configure an untagged LAN software bridge for this P0.
Run the VLAN items below only after T1-T5 pass on the untagged bridge.
This build offloads one bridge, VLAN-unaware (B checks) or VLAN-aware (VA
checks); a second bridge falls back to software bridging without opening
user-to-user isolation, with a "Range of offloadable bridges exceeded"
message. Failure to attach the port is not expected.

## 5. Capture a baseline after boot

Run this through the verified management path before starting traffic. Save the
output on the router or copy it to the host; do not publish ART contents,
unique MAC addresses, serial numbers, or wireless calibration data.

```sh
{
	date -u
	uname -a
	ubus call system board
	cat /etc/openwrt_release
	dmesg
	dmesg | grep -iE 'rtl837|dsa|phylink|mdio|smi|regmap|timeout|error|fail|oops|call trace'
	lsmod | grep -E 'rtl8372n|tag_rtl8_4|dsa' || true
	ip -br link
	ip -d link
	ip -s link
	bridge link
	bridge vlan show
	command -v ethtool >/dev/null && for d in lan1 lan2 lan3 lan4; do ethtool "$d" 2>&1; done
} 2>&1 | tee /tmp/rtl8372n-p0-baseline.txt
```

If an interface name differs, record the name and use it consistently. The
board's expected user-port names are `lan1` through `lan4`; WAN is a separate
RTL8221B path and is not part of this switch test.

## 6. Test matrix

Record **PASS**, **FAIL**, or **BLOCKED** for each row, with the relevant log
or command output. A blocked test is not a pass.

| ID | Scenario and procedure | Expected result |
| --- | --- | --- |
| T0 | Build/package gate above | Package and full image build cleanly; both required modules are present. |
| T1 | Cold boot with the verified recovery/management path available; save the full boot log and any pstore output. Search for the chip-ID log and driver errors. | Chip-ID read succeeds and passes RTL8372N detection; no probe failure, MDIO timeout, SDS timeout, DSA registration failure, kernel warning, or oops. |
| T2 | Check `ip -d link`, `bridge link`, and `lsmod`. | DSA ports `lan1`–`lan4` exist and join the configured LAN bridge; the CPU conduit is `xgmac1`/the board's `lan` path. `rtl8372n_dsa` and `tag_rtl8_4` are loaded. |
| T3 | Connect one peer to each physical LAN jack, one at a time. For each, record interface carrier and `ethtool` speed/duplex, then unplug and reconnect. | Jack map is LAN1→port 7, LAN2→6, LAN3→5, LAN4→4. Carrier follows the cable; no repeated flap or stuck port. Test 1G and 2.5G only when the peer supports those rates. |
| T4 | From one directly connected host, obtain the expected LAN address and ping the router's LAN address for at least 100 packets. Record loss and `ip -s link` before/after. | Router CPU path works through the switch; no persistent loss, growing error counters, or DSA/tagger errors. |
| T5 | Connect two independent hosts directly to two different LAN jacks, on the same untagged LAN. Confirm they are not connected through another bridge. Ping between them, then run `iperf3` for 30 seconds in both directions. Repeat across all six jack pairs; record any unavailable pair as BLOCKED. | ARP and bidirectional untagged LAN-to-LAN traffic work; no link reset, kernel warning, or persistent packet loss. Record rates as observations, not P0 pass thresholds. |
| T6 | Record `bridge vlan show` and the driver's `P0 readback` lines from `dmesg`. The readback runs during setup through the driver's regmap path. Its VLAN-table transaction issues a read command only; do not issue raw MDIO commands. | Host output records software configuration only. VLAN 1 table word, available-port PVIDs, isolation masks, learning limits, flood destinations and the VLAN-unaware port controls must report `PASS`: accepted frame types 0, ingress filter 0, one "VLAN egress transparent ports" line per available port (all available ports set) and egress tag mode REAL_KEEP (3) per available port, plus the global filter enable. The VLAN 0 line (the empty no-PVID entry, `data0=0x2000000`) precedes VLAN 1. Isolation and learning lines cover ports 0–9, and the control-frame lines of the CF checks follow. With five available ports that is 59 lines. A mismatch/read error is not a pass. Also run the standalone-port negative forwarding check below. VLAN-aware bridges are covered by the VA checks. |
| T7 | Reboot normally three times, then perform one full power-off/power-on with the verified recovery/management path available. Repeat T1–T4 after each boot. | Probe, link mapping, and basic CPU/LAN traffic remain consistent. No boot relies on stale switch state left by the previous run. |
| T8 | Optional: after all smoke tests pass, measure `iperf3` through a 2.5G-capable LAN peer in each direction. | Record peer, link speed, command, throughput, loss, and counters. No throughput target is defined for this unvalidated P0 candidate. |

### Additional P0 regression checks

- Capture each `/sys/bus/mdio_bus/devices/*/c45_phy_ids/mmd*_device_id` and
  its `driver` symlink. Ports 4–7 must be C45 devices with ID `0x001ccad0` in
  MMDs 1, 3, 7 and 31, and dmesg must show `lanN ... PHY [...:0N] driver
  [RTL8224 2.5Gbps PHY]`. A "no PHY driver bound" warning is a failure.
  `ethtool lanN` must list 2500baseT/Full as supported before any cable is
  connected. `ethtool -r lanN` on a linked jack must renegotiate without error,
  and `ethtool --cable-test lanN` on an idle jack must report per-pair results.
  `kmod-phy-realtek` also serves WAN; check its normal binding. Boot must reach usable
  management beyond the previously reported hang point; retain the full serial
  log and timing if it hangs again. Record supported, advertised and partner modes from
  `ethtool`, including a 2.5G-capable peer and 1G/100M peers where available.
- If registration aborts, save the full boot log before devres cleanup removes
  the child bus. Include `PHY discovery`, `PHY C45 read failed`, `no PHY
  driver bound` and completion-error messages for every scanned port.
  Missing sysfs nodes after cleanup do not distinguish scan failure from a
  binding failure. Keep T2–T8 BLOCKED until T1 passes.
- Before forming a bridge, verify a pair of standalone LAN ports does not
  forward between the hosts. Then attach the first port to an empty software
  bridge, attach the second, verify bidirectional traffic, remove each port
  and repeat. Removal must restore isolation. Capture errors and register
  masks at each stage. CPU load/traffic should reflect hardware forwarding
  for an offloaded bridge (B2) and software forwarding otherwise.
- Run concurrent `ethtool` reads and port down/up cycles from separate shells.
  Save PHY command errors, link recovery and all kernel/lockdep output. This
  exercises the shared PHY command engine; do not infer race freedom from a
  single pass.
- Capture physical switch and SoC PCS state and bidirectional CPU-link traffic.
  A DT fixed-link's 10,000 Mb/s carrier alone is not physical link evidence.
- After cold and warm boot, capture all driver `P0 readback` lines. Their P0
  targets are zero learning limits, CPU-only flood destinations, and an
  isolation matrix that sends user-port traffic only to the CPU port. If a
  readback line is absent, fails, or reports `MISMATCH`, keep T6 BLOCKED/FAIL
  and attach the full boot log; do not replace it with guessed raw MDIO
  commands.
- Unknown unicast, broadcast, multicast and reserved control-frame handling
  still need negative forwarding tests (CF checks below). Do not connect a
  physical loop before BPDU/RMA behavior has been verified on the isolated
  bench.

### Table engine checks

Setup now writes VLAN 1 through the shared table engine, verifies it, and
clears the L2 table. Either failure aborts setup with an error. The per-step
timings are `dev_dbg` lines: build with `-DDEBUG` or use dynamic debug to see
them.

| ID | Procedure | Pass condition |
| --- | --- | --- |
| TE1 | Cold and warm boot; search dmesg for `L2 table clear failed`, `VLAN 1 readback mismatch`, `reserved bits` and `table command`. With debug on, record `L2 table cleared in N us`. | Setup completes; none of the error lines appear. The clear time is recorded (it has never been measured). |
| TE2 | T6 `P0 readback VLAN 0` and `VLAN 1` lines. | `VLAN 0 data0=0x2000000 ... PASS` and `VLAN 1 data0=0x203e0f8 ... PASS`. Bits 25:0 are compared. |
| TE3 | Optional, on a lockdep (`CONFIG_PROVE_LOCKING`) build: boot and run T3/T4. | No lockdep report involving `table_lock`. |

### Control-frame and lookup-miss checks

Setup now traps 01:80:c2:00:00:00 and 02–0f (BPDU, LACP, EAPOL, LLDP; slot 01
PAUSE untouched) to the CPU through the RMA engine, points the RMA/PTP
(`0x4f34`) and L2 (`0x5350`) trap destinations at the external CPU, makes
lookup misses and unmatched source addresses forward per the flood masks,
turns ASIC IGMP/MLD snooping off, and writes STP FORWARDING on every
available port. None of this has run on hardware. Capture inbound frames on
the conduit before the tag is stripped:
`tcpdump -i lan -Q in -e -xx 'ether[0:4] = 0x0180c200'` (byte 15 REASON,
bytes 16–17 word 2, low nibble of byte 19 the source port).

| ID | Procedure | Pass condition |
| --- | --- | --- |
| CF1 | T6 lines `STP port states`, the five `... action` lines, `IGMP/MLD snooping`, `RMA/PTP trap CPU`, `L2 trap CPU`, seven `RMA link-local trap`, `RMA LLDP trap`, `RMA LLDP_EN`, and port 9 isolation/learning. | All PASS. Record the full values: bits outside the mask (storm bypass, trap priorities, other `0x5350` fields) are reset values never recorded before. |
| CF2 | Standalone lan1 with a peer, observer on standalone lan2. From the peer send about 30 each of 802.1D BPDUs (DA :00), LACPDUs (:02, 0x8809), EAPOL-Start (:03, 0x888e) and LLDP (:0e, 0x88cc). Capture on the conduit, `lan1` and the observer. | Each frame reaches `lan1` once with a non-zero REASON (predicted 0x5f; record it and word 2) and source port 7. No `non-realtek ethertype`, `unknown realtek protocol` or `could not find user for port` in dmesg. The observer sees none. |
| CF3 | Frames from user ports still reach the CPU: T4/T5, a DHCP lease, IPv6 ND, mDNS from the peer, and a unicast to an unknown MAC from the peer (seen on the conduit). Run `lldpd`, or a bridge with `stp_state 1`, and capture on the peer. | All reach the CPU with REASON 0. Router-originated unicast, broadcast, LLDPDUs and BPDUs still reach the peer (CPU-tagged frames are not trapped back). |
| CF4 | Optional A/B in a test build: `0x4f34` 17:16 = 0, then `0x5350` 16:15 = 0, repeating CF2. | Records which field steers RMA traps. |
| CF5 | Offloaded bridge lan1+lan2, with vlan_filtering 0 and 1 (and 1 with lan1's PVID removed), STP on and off; repeat CF2. Then move a station from lan1 to lan2. | The switch forwards no control frame to the observer. STP on: zero copies. STP off: exactly one copy of each BPDU, sent by the software bridge (zero means the image lacks the tagger patch `0457-net-dsa-tag_rtl8_4-...`; two, that the switch forwarded it as well); none with lan1's PVID removed, where the bridge drops untagged frames; EAPOL and LLDP only with `group_fwd_mask` 0x8 or 0x4000 on the bridge, then exactly one copy each; LACPDUs never. Traffic to the moved station recovers at once, not after ageing (`0x535c` is left at reset). |

If CF2 shows trapped frames arriving untagged or damaged, revert to P0's
forwarding of these groups and fall back to the static L2 multicast route of
MNeroba/openwrt-flint3#2; do not use bridge offload (keep the ports
standalone).

### L2 callback checks

DSA drives the CIST state: standalone ports FORWARDING when up and
DISABLED when down, the CPU port FORWARDING after setup, bridged ports as
the bridge says. Only a port that learned is fast-aged. The FDB, dump and
ageing callbacks run for the offloaded bridge. The per-operation lines are
`dev_dbg`: build with `-DDEBUG` or use dynamic debug.

| ID | Procedure | Pass condition |
| --- | --- | --- |
| L1 | With lan1–lan4 standalone, boot, then `ip link set lan1 down`, wait 2 s, `ip link set lan1 up`; repeat for lan2–lan4; then T4. | After setup `port 3: STP state 3, CIST 3`, and `STP state 0` for ports 0–2 and 8. For lan1: `port 7: STP state 0, CIST 0`, then `STP state 3, CIST 3`; no flush (learning is off; B6 covers the flush). No `L2 flush failed`, `flush mode left` or `failed to set STP state`. T4 passes. |
| L2 | Reboot, or remove the module. | No `L2 flush failed` for port 3 (the CPU port is never flushed). |
| L3 | `bridge fdb show` with every port standalone or in a software bridge. | No new `self` entries and no table-command error: such ports are not walked. |
| L4 | br-lan with lan1+lan2. `bridge fdb show`; `bridge fdb add 02:00:00:00:00:01 dev lan1 master static`; `bridge fdb show dev lan1`; `bridge fdb del 02:00:00:00:00:01 dev lan1 master`. Let a host on lan2 talk. | br-lan's MAC is programmed for port 3 at VID 1 (debug `static entry for port 3`). The static entry shows as `self`, `vlan 1`, `static`, then goes. The lan2 host shows as a learned `self` entry. No `L2 ... wrote`, `another key` or `still present after delete` error. |
| L5 | A learned host and a static entry on lan2, then `bridge link set dev lan2 learning off`. Then `ip link set br-lan type bridge ageing_time` 3000, 0 and 100000 (30 s, 0, 1000 s). | The learned entry leaves `bridge fdb show dev lan2` and the static one stays: the first proof that the flush removes learned entries only. Debug `L2 ageing time 30 s`, `14 s`, `800 s`; no change fails. |

### Bridge offload checks

One bridge is offloaded: join opens port isolation between its members, the
bridge flags drive learning and the flood-mask bits, and leave restores the
standalone state (CPU-only isolation, no flooding or learning, flush,
FORWARDING). The B checks use a `br-lan` without `bridge-vlan` sections
(VLAN-unaware); the VA checks below cover a VLAN-aware one. Run CF2 first,
and connect no physical loop before CF2, CF5 and B7 without the loop have
passed. Debug lines as for the L checks.

| ID | Procedure | Pass condition |
| --- | --- | --- |
| B1 | Boot with br-lan = lan1–lan4. `bridge link show`, dmesg. | `port N joined bridge 1, members ...` for each port, the last to join listing the other three (0xe0 if it is port 4), and `bridge flags ... bridged 1` lines. No `bridge join failed`. T4 passes on each jack. |
| B2 | Hosts on lan1 and lan2: `iperf3` 30 s each way (and `-P 4`), at 2.5G if possible; `mpstat`/`top` on the router and `ip -s link show lan` before/after. Repeat T5's pairs. Then ping between VID 10 uppers on the two hosts, capturing on both. | Near line rate with router CPU close to idle; the conduit counters barely move (LAN-to-LAN frames no longer cross the CPU). Compare with the T5 software numbers. The VID 10 ping passes with the tag (VID, PCP) unchanged on both hosts. |
| B3 | `ip link set lan4 nomaster`, address it on its own subnet, then ARP/ping between the lan4 host and the lan1 host's address, with captures on both hosts. Re-add lan4. Also put lan4 alone in a second bridge. | `port 4 left bridge 1`. The lan1 host sees no frame from the lan4 host and vice versa; lan4 still reaches the router (T4). The second bridge stays in software (no join line). Re-adding restores B1/B2. |
| B4 | Enslave a port to a second bridge (VLAN-aware or not) while br-lan is offloaded. | It enslaves with `Range of offloadable bridges exceeded` and forwards in software; its port stays CPU-only (B3). |
| B5 | From the lan1 host send unknown unicast (static ARP for an absent MAC), unknown multicast (e.g. `iperf3 -u` to 239.1.1.1 or mDNS) and broadcast (ARP); capture on lan2's host. Toggle `bridge link set dev lan2 flood off`, `mcast_flood off`, `bcast_flood off`, then back on. Try `hairpin on` and `isolated on`. | Each class stops reaching lan2 exactly when its flag is off and returns when on; the router (CPU) keeps seeing all of them. `hairpin`/`isolated` are refused. No broadcast is ever echoed back to its sender (`0x4f84` is left at reset). |
| B6 | Hosts talking on lan1 and lan2; `bridge fdb show dev lan2`. Unplug lan2's cable, wait, show again; replug. | The lan2 host is a learned `self` entry; on unplug `port 6: STP state 0`, `port 6: dynamic L2 entries flushed (cmd 0x40)` and the entry is gone; it is learned again after replug. Static entries (L4) survive. |
| B7 | `ip link set br-lan type bridge stp_state 1`, then re-enslave a port; capture BPDUs on its peer. Then L4 with traffic to the static MAC. | CIST lines 1 (listening/blocking), 2, then 3 after 2 × forward_delay; no LAN-to-LAN traffic before 3, BPDUs reach the peer in every state. Traffic to the static MAC leaves only lan1, and router-bound unicast from lan1 is not flooded to lan2. |
| B8 | Remove every port from br-lan, one at a time, then re-add. | `port N left bridge 1` each time, no error line; then the standalone negative forwarding check and T4 pass on each removed port. |

### VLAN-aware bridge checks

A VLAN-filtering bridge is offloaded: bridge VLANs become table entries, a
filtering port gets the ingress filter, the bridge PVID (tagged-only and
PVID 0 without one) and untagging per VLAN (egress ORIGINAL), and loses its
egress-membership exemption; VLAN 1 keeps the CPU and every VLAN-unaware
port. None of this has run on hardware. Suggested `bridge-vlan` setup on
`br-lan` (vlan_filtering 1): lan1 is a trunk to an AP (VID 1 untagged PVID,
VIDs 10 and 20 tagged), lan2 an access port of VID 10, lan3 of VID 20, lan4
of VID 1 (each untagged PVID); br-lan itself VID 1 untagged and VIDs 10/20
tagged, with `br-lan.10`/`br-lan.20` addressed. Debug lines (`VLAN N entry`,
`VLAN filtering`, `VLAN N added/deleted`, `bridge PVID`) need dynamic debug.
Capture on the conduit with `tcpdump -i lan -e -xx` as in the V checks.

| ID | Procedure | Pass condition |
| --- | --- | --- |
| VA1 | Boot with the setup above; `bridge vlan show`, dmesg. | Joins as in B1, then `port N: VLAN filtering 1` and per-port `VLAN N added` lines matching `bridge vlan show`. Last entry words: `VLAN 1 entry 0x02026098` (CPU, lan1, lan4; lan2/lan3 dropped), `VLAN 10 entry 0x020100c8` (CPU and lan1 tagged, lan2 untagged), `VLAN 20 entry 0x020080a8`. No `failed`, `not restored` or readback-mismatch line. |
| VA2 | Trunk: from the AP send VID 10 and VID 20 tagged traffic to hosts on lan2 and lan3, and back; `iperf3` both ways with `mpstat` on the router. | Frames reach lan2/lan3 untagged and the AP tagged with the right VID (PCP kept); hardware-forwarded (router CPU near idle, conduit counters still). |
| VA3 | Isolation: hosts on lan2 and lan3 on one subnet, ARP/ping between them; tagged VID 20 into lan2; VID 30 (configured nowhere) tagged into lan1; priority-tagged (VID 0, PCP 5) frames into lan2. | Nothing crosses between VID 10 and VID 20 except via the router; VID 20 on lan2 and VID 30 on lan1 are dropped (seen on no port, not on the conduit). Priority-tagged frames from lan2 go to VID 10 like untagged ones and reach the conduit still VID-0 tagged; if they are dropped, record it (`0x4e14` bit 0, VID 0 type, is at reset). |
| VA4 | CPU: hosts ping `br-lan.10`/`br-lan.20` addresses, the AP pings over VID 10 tagged; conduit capture. | Frames from access ports reach the conduit untagged, from the trunk tagged (no extra tag behind the head tag). Router frames leave access ports **untagged** and the trunk tagged. A VID 1 tag on a router frame at lan2/lan3 means the CPU tag's KEEP bit does not override ORIGINAL: stop and report (fallback: keep VLAN-aware bridges in software). |
| VA5 | `bridge vlan add dev lan2 vid 10` (no `pvid`), then send untagged, priority-tagged and VID 10 tagged frames from lan2's host; then restore `pvid untagged`. | Debug `bridge PVID 0`. Untagged and priority-tagged frames are dropped (on no port, not at the CPU, not in VLAN 1 at lan4); tagged VID 10 still passes. Restoring the PVID restores untagged traffic. |
| VA6 | Toggle at runtime: `ip link set br-lan type bridge vlan_filtering 0`, test, then `1`, test; repeat three times. Run VA2/VA3 under 1 and B2 under 0. | Every toggle logs `VLAN filtering 0/1` per port without errors; under 0 every port is one domain with tags passing unchanged, under 1 VA2–VA4 hold again. |
| VA7 | `ip link set lan2 nomaster`, then re-add it; also delete br-lan with all ports in it. | `port 6 left bridge 1`, `VLAN filtering 0`, the `VLAN 1 entry` includes the port again; the standalone negative check, T4 and V1 pass on it. Re-adding restores VA1. |
| VA8 | A VID present only on switch ports (`bridge vlan add dev lan2 vid 40`, same on lan3, not on br-lan): traffic between their hosts on VID 40; capture on the conduit. | Forwarded lan2↔lan3, but no VID 40 copies reach the CPU. |
| VA9 | BPDU/LLDP from lan2's peer (PVID 10) with STP on; then in VA5's no-PVID state. | They reach the CPU (RMA trap, VLAN-leaky) in both states and no other port. |
| VA10 | PPE: a routed LAN->WAN flow from lan4's host while br-lan is VLAN-aware; then toggle vlan_filtering with an offloaded flow live. | Offload towards ports of the VLAN-aware bridge is declined (0449); after the toggle no frame leaves a LAN port with a leaked VID 1 tag. |

### VLAN-unaware port checks

Standalone ports and ports of a VLAN-unaware bridge are VLAN-unaware; 802.1Q
traffic should pass through the CPU unchanged.
Capture on both the peer and the router, and on the conduit with
`tcpdump -i lan -e -xx` to see the raw `0x8899` head tag.

| ID | Procedure | Pass condition |
| --- | --- | --- |
| V1 | `ip link add link lan1 name lan1.10 type vlan id 10`, address it, ping a peer that tags VID 10. | Tagged VID 10 frames both ways; ARP broadcast reaches `lan1.10`. |
| V2 | With lan1 and lan2 out of br-lan, put them in a second, software bridge with `vlan_filtering 1` (lan1 tagged VID 10, lan2 untagged PVID 10) while br-lan holds the hardware bridge. Ping between the peers. | Frames cross the CPU with the right tag on each side (the VA checks cover the offloaded case). |
| V3 | Send VID 1 tagged and priority-tagged (VID 0, PCP != 0) frames from a peer. | They reach the CPU still tagged, with VID and PCP unchanged. |
| V4 | 1522-byte tagged frames (`ping -s 1472` over a VLAN upper). | No drop or fragmentation over the CPU link. |
| V5 | Repeat the standalone-port negative forwarding check. | No user-to-user leak. |
| V6 | On the conduit capture, check head-tag bytes 16-17 (word 2) for untagged, priority-tagged and VID 10 frames from a user port. | `0000` in every case; the PPE head-tag alias depends on it. Then re-check PPE flow offload LAN->WAN and WAN->LAN. |

### Do not run these as P0 tests

- Do not run `tests/bridge-flags.sh` as is: it aborts at its first `hairpin`
  readback (this driver refuses `hairpin` and `isolated`), before any
  forwarding check. B5 and L5 cover the supported flags.
- Do not test FDB/MDB or STP offload beyond the L, B and VA checks, LAG (#47), rate
  limiting (#49), EEE, or switch GPIO. They are out of scope and can produce
  misleading failures.
- Do not compare the candidate to the former 1.8–1.9 Gbit/s result as a pass
  criterion. That measurement belongs to the previous driver build.

## 7. Failure triage

| Symptom | First evidence to attach | Likely area to investigate |
| --- | --- | --- |
| No RTL8372N chip-ID log / probe fails | Full boot/dmesg log and any pstore output; MDIO node and address; chip-ID read result if available | MDIO transport, address 29, reset timing, chip-ID mask. |
| Probe succeeds but DSA ports are absent | Probe log, `ip -d link`, DT fragment and build config | DSA registration, CPU/user port topology, required tagger module. |
| CPU link reports up but router traffic fails | Both ends' link state, `ip -s link`, tagger/EDMA logs, all four DTS polarity flags | RTL8_4 path, SDS mode/polarity, CPU port 3 mapping, post-reset SerDes init. |
| One or more LAN ports have no carrier | Physical jack, expected DSA port, `ethtool`, PHY/MDIO errors | Port map, internal PHY power-up sequence, PHY access. |
| Link is up but LAN-to-LAN traffic fails | Host interface/IP setup, pair under test, ARP table, packet captures at both hosts, bridge and VLAN output | Isolation, VLAN 1 membership/PVID, dynamic learning, DSA forwarding. |
| Warm boot works but cold boot fails (or vice versa) | Separate logs for each cycle and reset/power sequence | Switch reset and power-on timing; reliance on bootloader state. |

Change one variable at a time. For a polarity experiment, first preserve the
known-good all-four-swaps DTS and a recovery image; record each exact change
and boot result. Do not edit the live device tree on the only reachable test
unit.

## 8. Maintainer report template

Copy this block into the issue/PR report and fill every field. Mark unrun steps
as `BLOCKED` or `NOT RUN`, not `PASS`.

```text
Board / hardware revision:
Switch package marking (if readable):
Test date and operator:
Source commit:
Kernel release:
Image SHA-256:
Feed revisions / attached p0-feeds.conf:
Build host / build result:
DT compatible / MDIO address:
CPU port / conduit:
SerDes polarity properties enabled:
Chip ID from boot log:
PHY IDs / bound drivers per port:
Supported / local / partner advertised link modes per port:
Physical switch and SoC PCS evidence:
T6 driver P0-readback lines (VLAN/PVID/isolation/learning/flood/filter):
Standalone / bridge create-remove results:
Concurrent PHY access / port lifecycle results:
Reserved control-frame / negative forwarding results:
T0 build:
T1 probe:
T2 DSA topology:
T3 LAN jack-to-port and link-rate matrix:
T4 router reachability (sent / received / loss):
T5 LAN-pair matrix and iperf3 results:
T6 VLAN 1 output:
T7 warm/cold boot results:
T8 optional throughput:
Kernel warnings, timeouts, or errors:
Attached logs (with MAC/serial/calibration data redacted):
Overall result and first blocker:
```

## 9. Request for the driver maintainer

The first useful review from a maintainer with RTL8372N hardware is to check
whether the P0 sequence is sufficient after a software reset: chip detection,
SDS mode selection, both mode-specific polarity registers, internal PHY power
control, RTL8_4 CPU tagging, and VLAN 1 bootstrap. Please report the exact
silicon/board, source revision, boot log, per-port link states, and whether
untagged CPU↔LAN and LAN↔LAN traffic pass. If traffic fails, include the first
failing matrix row and the logs requested above; no vendor PHY patch dump is
needed for this P0 report.
