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
for the P0 evidence and its remaining gates. Start each run only after T0 passes for the
exact image revision under test.

**This revision** (branch `rtl8372n/p1-bridge-offload`) adds hardware bridge
and VLAN offload, on unless the module parameter `offload=0` is set; none of
it has run on hardware. Build the bench image with the debug option and
follow the bench order in section 4, which starts with `offload=0`.

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
5. Internal PHY ports bind to `RTL8224 2.5Gbps PHY`, report link
   changes and carry untagged traffic to the router. In P0 LAN-to-LAN traffic
   ran through the software bridge and CPU; this build offloads one bridge,
   VLAN-unaware or VLAN-aware, to the switch (see the B and VA checks).
6. These results survive warm reboot and a cold power cycle.

Beyond the L, B, AL, U and VA checks, the run does **not** qualify FDB/MDB
offload, STP, LAG, rate limiting, GPIO, EEE, or performance parity. Do not use
tests for those features as acceptance criteria.

The future P1 acceptance matrix is in [P1-RESEARCH.md](P1-RESEARCH.md). This
build implements its P1-A to P1-D steps together (see its section 1); their
gates are the phases of section 4 below. In particular, establish CPU-only
BPDU reception (CF2), CF5 and a loop-free B7 before any physical loop.

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
  log collection.
- A management path that does not use lan1–lan4, required for every bench
  phase since the checks take LAN ports down, out of br-lan or into
  untested offload: a serial console if the unit has a working one, Wi-Fi
  on network `lan` set up in the image's config (it also serves AL1/AL2),
  or SSH over WAN (the `ap.config` image has no firewall). Record which.
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
echo 'CONFIG_RTL837X_DSA_DEBUG=y' >> .config   # the bench image's dev_dbg lines
make defconfig
make package/kernel/rtl837x/clean
make package/kernel/rtl837x/compile V=s
make -j"$(nproc)"
```

The bench image must be a debug build: most pass conditions below are
`dev_dbg` lines, and the target kernel has no dynamic debug, so without
`CONFIG_RTL837X_DSA_DEBUG` they are compiled out. Every check that relies
on a debug line, or on its absence ("no `joined bridge` line"), counts only
on an image that passed the debug part of T0/T1. Put the phase-1 module
option in a `files/etc/modules.conf` overlay (bench order below) and keep
the overlay with the build record.

The PPE checks (V6's flow-offload part, B2's routed flows, VA10) need
hardware flow offload, which `ap.config` leaves out (no firewall4,
nftables, conntrack or `kmod-nft-offload`). Run them on an image of the
default device profile built the same way, with firewall4's
`flow_offloading` and `flow_offloading_hw` on (its firewall closes SSH over
WAN), or record them BLOCKED.

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
without errors, the kernel build log shows
`0457-net-dsa-tag_rtl8_4-do-not-mark-trapped-link-local-frames-as-forwarded.patch`
applied (`rtl8_4_read_tag()` in build_dir's `net/dsa/tag_rtl8_4.c` calls
`is_link_local_ether_addr()`), and the image contains `rtl8372n_dsa.ko` plus the kernel's
`tag_rtl8_4` module. For the bench image, `grep -ac 'joined bridge'` on the
built `rtl8372n_dsa.ko` must be non-zero (only a debug build carries the
debug strings). Keep the build log with the test report. Install only by
the project's established recovery/install procedure on a recoverable test
unit; this document deliberately gives no unverified flash command.

### Bench order

By default this build offloads a bridge as soon as a LAN port joins one, and
the board's default `br-lan` (lan1–lan4) is offloaded at the first boot.
Phases 1 and 2 therefore run with the module parameter `offload=0`, which
must be in place before the candidate's first boot: build the image with a
`files/etc/modules.conf` overlay holding `options rtl8372n_dsa offload=0`,
or add that line to `/etc/modules.conf` on a known-good OpenWrt image and
upgrade keeping settings (never `-n`; an install from stock firmware resets
the config, so use the overlay there). After each phase-1/2 boot,
`/sys/module/rtl8372n_dsa/parameters/offload` reads `N`.

Run the phases in order, each only after the previous one passed; on a
failure, report it and go back to the previous phase's configuration
(software mode below at runtime, or `offload=0` and a reboot). Prefer
runtime changes (`ip`, `bridge`, no `uci commit`) where a check allows, so
that a reboot returns to the booted configuration; for the checks that need
a boot (B1, VA1) keep a copy of the previous `/etc/config/network` to put
back over the management path. Connect no physical loop before CF2, CF5
and a loop-free B7 have passed, and none on a VLAN-aware bridge before
CF5's VLAN-aware cases (phase 4).

1. **P0 and setup** (`offload=0`; the default br-lan holds lan1–lan4 in
   software). Run T1, T2, T3, T4, T5 and the bridge create/remove check,
   T6, TE1 (cold boot and each T7 boot), TE2, CF1, CF3, L1, L2 and L3. For
   the standalone checks (T6's standalone negative check, CF2, CF4) take
   the ports under test out of br-lan (`ip link set lanN nomaster`; the
   board's `20-dsa-port-up` hotplug script has already brought them up)
   and put them back afterwards. Run T7: the option persists across the
   reboots. A `hardware bridging disabled` line with any other reason than
   `module parameter offload=0` ends the bridge phases: record it and the
   error before it; the build then behaves as P0.
2. **VLAN-unaware ports** (still `offload=0`). V1–V6, on standalone ports
   or in br-lan.
3. **Offloaded VLAN-unaware bridge.** Remove the option (`sed -i
   '/rtl8372n_dsa/d' /etc/modules.conf`) and reboot with br-lan = lan1–lan4
   and no `bridge-vlan` sections: the parameter file reads `Y` and no
   `module parameter offload=0` line appears. B1 first, then CF5's
   vlan_filtering 0 cases, B2–B12, L4, L5, AL1, AL2, U1–U3, CF7, TE4 and
   TE3's B6/L4 part; optionally CF6.
4. **Offloaded VLAN-aware bridge.** The `bridge-vlan` setup of the VA
   checks: VA1, then VA9, then CF5's vlan_filtering 1 and no-PVID cases,
   VA2–VA8, VA10, VA11, U4, U5, TE3's VA6 part, and TE1's warm variant
   with this image.

*Software mode* keeps a bridge in software at runtime with offload on, the
fallback when phase 3 or 4 fails: a port that has an 8021q upper when it
joins is refused by the driver (`Port has 8021q uppers, bridged in
software`), and DSA bridges it through the CPU as in P0.

```sh
for p in lan1 lan2 lan3 lan4; do
	ip link set "$p" nomaster
	ip link add link "$p" name "$p.4000" type vlan id 4000
	ip link set "$p" master br-lan
	ip link set "$p" up
done
```

`ip link set ... master` prints the extack as a warning (with `offload=0`:
`hardware bridging disabled: module parameter offload=0 at setup`); netifd
enslaves through an ioctl that carries none, so a bridge it builds shows no
message. Either way LAN-to-LAN traffic moves the conduit counters, and a
debug image logs no `joined bridge` line (with `offload=0`, `port N:
bridged in software: module parameter offload=0` instead). A second bridge,
or any bridge while setup disabled offload, falls back to software in the
same way without opening user-to-user isolation.

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
| T0 | Build/package gate above | Package and full image build cleanly; both required modules are present; the bench image is a debug build (`joined bridge` string in the module). |
| T1 | Cold boot with the verified recovery/management path available; save the full boot log and any pstore output. Search for the chip-ID log and driver errors. | Chip-ID read succeeds and passes RTL8372N detection; no probe failure, MDIO timeout, SDS timeout, DSA registration failure, kernel warning, or oops. The debug image's boot log has `port 3: STP state 3, CIST 3` and `L2 table cleared in N us`; without them every debug-line check (and every absence check) is void. |
| T2 | Check `ip -d link`, `bridge link`, and `lsmod`. | DSA ports `lan1`–`lan4` exist and join `br-lan` (phase 1, `offload=0`: one `hardware bridging disabled: module parameter offload=0` line, debug `port N: bridged in software: ...` per port, no `joined bridge` line, LAN-to-LAN traffic moves the conduit counters); the CPU conduit is `xgmac1`/the board's `lan` path. `rtl8372n_dsa` and `tag_rtl8_4` are loaded. |
| T3 | Connect one peer to each physical LAN jack, one at a time. For each, record interface carrier and `ethtool` speed/duplex, then unplug and reconnect. | Jack map is LAN1→port 7, LAN2→6, LAN3→5, LAN4→4. Carrier follows the cable; no repeated flap or stuck port. Test 1G and 2.5G only when the peer supports those rates. |
| T4 | From one directly connected host, obtain the expected LAN address and ping the router's LAN address for at least 100 packets. Record loss and `ip -s link` before/after. | Router CPU path works through the switch; no persistent loss, growing error counters, or DSA/tagger errors. |
| T5 | Connect two independent hosts directly to two different LAN jacks, on the same untagged LAN. Confirm they are not connected through another bridge. Ping between them, then run `iperf3` for 30 seconds in both directions. Repeat across all six jack pairs; record any unavailable pair as BLOCKED. | ARP and bidirectional untagged LAN-to-LAN traffic work; no link reset, kernel warning, or persistent packet loss. Record rates as observations, not P0 pass thresholds. Run in phase 1 (`offload=0`) and compare with Release 7; B2 repeats it offloaded. |
| T6 | Record `bridge vlan show` and the driver's `P0 readback` lines from `dmesg`. The readback runs during setup through the driver's regmap path. Its VLAN-table transaction issues a read command only; do not issue raw MDIO commands. | Host output records software configuration only. VLAN 1 table word, available-port PVIDs, isolation masks, learning limits, flood destinations and the VLAN-unaware port controls must report `PASS`: accepted frame types 0, ingress filter 0, one "VLAN egress transparent ports" line per available port (all available ports set) and egress tag mode REAL_KEEP (3) per available port, plus the global filter enable. The VLAN 0 line (the empty no-PVID entry, `data0=0x2000000`) precedes VLAN 1, and VLAN 2, 4094 and 4095 follow with no member, untag or FID/MSTI bit (`mask=0xffffff`, left empty by the table reset; record whether bits 25:24 read set). Each available port also gets a `VLAN egress keep ports` line (0, never written). Then `VLAN table reset` (0), `VLAN reserved VID types` (bits 1:0 = 2), two `VLAN learning disable` lines (0) and `port-based FID enable` (0 for available ports). Isolation and learning lines cover ports 0–9, and the control-frame lines of the CF checks follow. After the egress tag mode comes `CPU tag-aware ports` (ports 0-8: the CPU port alone, `0x8`; bit 9, the 8051, as read). With five available ports that is 79 lines; record the whole values, since bits outside the masks are reset values never recorded before. A mismatch/read error is not a pass. A mismatch or failed read of a line only bridge offload relies on (VLAN 0, VLAN 2/4094/4095, egress transparency and keep, VLAN table reset, VID types, learning disable, port-based FID, and the CF1 action and trap lines) also logs `hardware bridging disabled: VLAN readback mismatch`, `L2 action readback mismatch` or `trap readback mismatch`. Also run the standalone-port negative forwarding check below. VLAN-aware bridges are covered by the VA checks. |
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
  forward between the hosts. Then attach the first port to an empty
  bridge, attach the second, verify bidirectional traffic, remove each port
  and repeat. Removal must restore isolation. Capture errors at each stage.
  Run it in phase 1 (`offload=0`) and again offloaded (phase 3, B3/B8):
  CPU load/traffic should reflect software forwarding in the first case and
  hardware forwarding (B2) in the second.
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

### Table engine and setup checks

Setup now resets the VLAN table (TABLE_RST; VIDs 2, 4094 and 4095 must then
have no member, untag or FID/MSTI bit, else every VID from 2 up is written
empty), pins the VLAN classification controls, writes VID 0 (empty) and
VLAN 1 through the shared table engine with a readback, and clears the L2
table. Apart from VLAN 1 (P0's entry), none of this has run on hardware. A
failed reset, a failed VID 0 write, a VLAN 1 readback
mismatch, a failed write of the pinned VLAN controls, L2 actions or
link-local trap, or a failed L2 clear logs `hardware bridging disabled:
<reason>` and setup goes on with every bridge in software; a VLAN 1 I/O
error or timeout, or an error in another step standalone ports need, ends
in `switch setup failed` (no LAN). `offload=0` sets the same gate on
purpose (`module parameter offload=0`) and changes nothing else in setup.
The per-step timings are `dev_dbg` lines (debug image, section 4).

| ID | Procedure | Pass condition |
| --- | --- | --- |
| TE1 | Cold boot, each T7 reboot, and a warm reboot from an image whose VLAN table held VID 2 or 4094 (the SDK-based image before installing, or this one after phase 4, with a VLAN-aware br-lan using one of them: only VIDs 2, 4094 and 4095 are sampled). Search dmesg for `hardware bridging disabled` (other than `module parameter offload=0`), `switch setup failed`, `readback mismatch` (any VID: `VLAN 0 ...` or `VLAN 1 ...`), `TABLE_RST`, `left entries set`, `not zeroed`, `L2 table clear failed`, `reserved bits` and `table command`. Record `VLAN table before reset` and `VLAN table reset in N us`, and with debug `L2 table cleared in N us`. | Setup completes; none of the error lines appear; the samples have bits 23:0 clear after the reset (bits 25:24 alone do not trigger the sweep: record them). Non-zero `before reset` words after the warm reboot show that the chip reset keeps the table; zero words say nothing unless the old image wrote a sampled VID (VA3's warm variant tests an unsampled one). Record the times (never measured). On a `hardware bridging disabled` line with another reason than `module parameter offload=0`, record the reason and the error line before it, check that standalone ports pass T4 and V1–V6 and br-lan works in software, and stop before phase 3. |
| TE2 | T6 `P0 readback VLAN` and `VLAN table reset` lines. | `VLAN 0 data0=0x2000000 ... PASS`, `VLAN 1 data0=0x203e0f8 ... PASS`, VLAN 2, 4094 and 4095 `... PASS` and `VLAN table reset ... PASS` (no later reset wiped VIDs 0 and 1). Bits 25:0 are compared for VIDs 0 and 1, bits 23:0 for the others. |
| TE3 | Optional, on a lockdep (`CONFIG_PROVE_LOCKING`) build: boot and run T3/T4, then B6 and L4 (phase 3) and VA6 (phase 4). | No lockdep report involving `table_lock`, `vlan_lock` or `fdb_lock`. |
| TE4 | Optional test build with debug reads, in phase 3: `0x5cb0` method, port and ENTRY_CLR fields after boot; the age read back after a static add (L4); the ACT_STS of a delete write; a method-7 walk compared with a method-3 walk of the same port; ACT_STS on a full bucket (B11). | Records the SDK-only table semantics of [PROVENANCE.md](PROVENANCE.md): fields 0 after boot; the age read back (6 is written) and both ACT_STS values; ACT_STS 0 on a full bucket is the driver's -ENOSPC; the two walks list the same entries. |

### Control-frame and lookup-miss checks

Setup now traps 01:80:c2:00:00:00 and 02–0f (BPDU, LACP, EAPOL, LLDP; slot 01
PAUSE untouched) to the CPU through the RMA engine, points the RMA/PTP
(`0x4f34`) and L2 (`0x5350`) trap destinations at the external CPU, makes
lookup misses forward per the flood masks and the new-SA (`0x4f6c`),
unmatched-SA (`0x4f70`), hash-full (`0x5350` 7:6) and learn-limit (`0x4f80`)
actions forward, handles a lookup hit outside the ingress port's isolation
mask as unknown (`0x5350` bit 8), clears the
user ports' source-port permit (`0x4f84`), turns ASIC IGMP/MLD snooping off,
and writes STP FORWARDING on every
available port. None of this has run on hardware. Capture inbound frames on
the conduit before the tag is stripped:
`tcpdump -i lan -Q in -e -xx 'ether[0:4] = 0x0180c200'` (byte 15 REASON,
bytes 16–17 word 2, low nibble of byte 19 the source port).

| ID | Procedure | Pass condition |
| --- | --- | --- |
| CF1 | T6 lines `STP port states`, the six `... action` lines (new and unmatched source address included), `IGMP/MLD snooping`, `RMA/PTP trap CPU`, `L2 trap CPU`, `L2 lookup-hit isolation action`, `L2 hash-full action`, `learn-limit action`, `source port permit`, `SA-moving forbid` (value only), seven `RMA link-local trap`, `RMA LLDP trap`, `RMA LLDP_EN`, and port 9 isolation/learning. | All PASS, and no `L2 action readback mismatch` or `trap readback mismatch` gate line (a failed action, snooping, trap CPU or RMA line keeps every bridge in software; the STP and port 9 lines are informational). Record the full values: bits outside the mask (storm bypass, trap priorities, other `0x5350` fields, the CPU bit of `0x4f84`) are reset values never recorded before. Record the `SA-moving forbid` value (`0x535c`, never written, polarity unknown): CF5's station move and AL2's roams are what tell its meaning. |
| CF2 | Standalone lan1 with a peer, observer on standalone lan2. From the peer send about 30 each of 802.1D BPDUs (DA :00), LACPDUs (:02, 0x8809), EAPOL-Start (:03, 0x888e) and LLDP (:0e, 0x88cc), and a few frames each to :04, :08 and :0d (RMA slots 4–6). Capture on the conduit, `lan1` and the observer. | Each frame reaches `lan1` once with a non-zero REASON (predicted 0x5f, the same for every DA; record it and word 2) and source port 7. No `non-realtek ethertype`, `unknown realtek protocol` or `could not find user for port` in dmesg. The observer sees none. |
| CF3 | Frames from user ports still reach the CPU: T4/T5, a DHCP lease, IPv6 ND, mDNS from the peer, and a unicast to an unknown MAC from the peer (seen on the conduit). Run `lldpd`, or a bridge with `stp_state 1`, and capture on the peer. | All reach the CPU with REASON 0. Router-originated unicast, broadcast, LLDPDUs and BPDUs still reach the peer (CPU-tagged frames are not trapped back). |
| CF4 | Optional A/B in a test build: `0x4f34` 17:16 = 0, then `0x5350` 16:15 = 0, repeating CF2. | Records which field steers RMA traps. |
| CF5 | Offloaded bridge lan1+lan2, with vlan_filtering 0 (phase 3), then 1 and 1 with lan1's PVID removed (phase 4, after VA1 and VA9, on the VA setup: lan2 has no VLAN 1 there, so observe on lan4, which shares lan1's VLAN 1, and move the station to lan4), STP on and off, each with the bridge's default `group_fwd_mask` and with 0x4008; repeat CF2. Then move a station from lan1 to lan2. | The switch forwards no control frame to the observer; every copy there is sent by the software bridge. BPDUs: zero copies with STP on; with STP off exactly one of each (zero means the image lacks the tagger patch `0457-net-dsa-tag_rtl8_4-...`; two, that the switch forwarded it as well). EAPOL and LLDP: zero copies with the default mask; with 0x4008 exactly one of each, with STP on or off (the bridge forwards `group_fwd_mask` frames whatever its STP state). LACPDUs never. None at all with lan1's PVID removed, where the bridge drops untagged frames. Traffic to the moved station recovers at once, not after ageing (`0x535c` is left at reset). |
| CF6 | Optional, after CF5 and B7, on an isolated bench only (never the live network): two RSTP switches on lan1 and lan2 with a link of their own, br-lan STP off. Also capture on a Wi-Fi client with STP off. | One switch blocks a port; no broadcast storm builds up. The Wi-Fi client sees each BPDU from lan1 once. |
| CF7 | Phase 3, offloaded br-lan with lan1–lan3: from the lan1 host send a few frames each to 01:80:c2:00:00:10, :20 and :21 (MRP/GARP), CDP (01:00:0c:cc:cc:cc), Cisco SSTP (01:00:0c:cc:cc:cd) and L2 PTP (01:1b:19:00:00:00, 0x88f7). Capture on the conduit (filter on each DA), the lan2 host and the lan3 host. | Each frame reaches the lan2 and lan3 hosts exactly once: switched in hardware, REASON 0 at the conduit, and the bridge sends no second copy. None there, with a non-zero REASON at the conduit, means that DA's reset action traps (the driver never writes these words) and `0457` still marks the frame as forwarded (README limitation): record the DA, REASON and word 2. Two copies mean the switch forwarded it and the bridge relayed it as well. |

If CF2 shows trapped frames arriving untagged or damaged, stop before phase
3 and keep `offload=0` (or the ports standalone). The fix is a
build that reverts the RMA slots to P0's forwarding, keeps bridge offload off
and drops the tagger patch, with the static L2 multicast route of
MNeroba/openwrt-flint3#2 as the documented fallback.

### L2 callback checks

DSA drives the CIST state: standalone ports FORWARDING when up and
DISABLED when down, the CPU port FORWARDING after setup, bridged ports as
the bridge says. Only a port that learned is fast-aged. The FDB, dump and
ageing callbacks run for the offloaded bridge. The per-operation lines are
`dev_dbg` (debug image, section 4).

| ID | Procedure | Pass condition |
| --- | --- | --- |
| L1 | Phase 1 (`offload=0`: every LAN port is standalone in the switch, in br-lan or not): boot, then `ip link set lan1 down`, wait 2 s, `ip link set lan1 up`; repeat for lan2–lan4; then T4. | After setup `port 3: STP state 3, CIST 3`, and `STP state 0` for ports 0–2 and 8. For lan1: `port 7: STP state 0, CIST 0`, then `STP state 3, CIST 3`; no flush (learning is off; B6 covers the flush). No `L2 flush failed`, `flush mode left` or `failed to set STP state`. T4 passes. |
| L2 | Reboot, or remove the module. | No `L2 flush failed` for port 3 (the CPU port is never flushed). |
| L3 | `bridge fdb show` with every port standalone or in a software bridge. | No new `self` entries and no table-command error: such ports are not walked. |
| L4 | br-lan with lan1+lan2. `bridge fdb show`; `bridge fdb add 02:00:00:00:00:01 dev lan1 master static`; `bridge fdb show dev lan1`; `bridge fdb del 02:00:00:00:00:01 dev lan1 master`. Let a host on lan2 talk. Then add and delete a static entry for that host's MAC on lan2 while it runs a ping flood. | br-lan's MAC is programmed for port 3 at VID 1 (debug `static entry for port 3`). The static entry shows as `self`, `vlan 1`, `static`, then goes. The lan2 host shows as a learned `self` entry, again after its static entry is deleted. No `L2 ... wrote`, `another key`, `static entry still present after delete` or DSA `failed to delete` line (debug `deleted, then found on port 6 (static 0)` may appear). |
| L5 | A learned host and a static entry on lan2, then `bridge link set dev lan2 learning off`. Then `ip link set br-lan type bridge ageing_time` 3000, 0 and 100000 (30 s, 0, 1000 s). Finally learning on, `ageing_time 3000`, and a lan2 host that sends one frame and goes silent; poll `bridge fdb show dev lan2`. | The learned entry leaves `bridge fdb show dev lan2` and the static one stays: the first proof that the flush removes learned entries only. With learning off, one-way UDP from the lan2 host to the lan1 host (`iperf3 -u`, or `mausezahn` to its MAC) is still forwarded in hardware: the conduit counters barely move and the lan3 host sees no copy of the stream. Replies towards the lan2 host are unknown unicast while its learning is off and flood to the CPU and every flood-enabled port (as in B5), so ping or TCP moves the conduit counters. Debug `L2 ageing time 30 s`, `14 s`, `800 s`; no change fails. The silent host's `self` entry goes after about 30–60 s: record the time (about 5× off means the ×5 unit is wrong). |

### VLAN-unaware port checks

Standalone ports and ports of a VLAN-unaware bridge are VLAN-unaware; 802.1Q
traffic should pass through the CPU unchanged. Run these in phase 2, on
standalone ports or in br-lan with `offload=0` (section 4).
Capture on both the peer and the router, and on the conduit with
`tcpdump -i lan -e -xx` to see the raw `0x8899` head tag.

| ID | Procedure | Pass condition |
| --- | --- | --- |
| V1 | `ip link add link lan1 name lan1.10 type vlan id 10`, address it, ping a peer that tags VID 10. | Tagged VID 10 frames both ways; ARP broadcast reaches `lan1.10`. |
| V2 | Delete V1's `lan1.10` first: an 8021q upper takes its VID's frames before the bridge sees them. Turn on `vlan_filtering 1` on br-lan (in software, `offload=0`), lan1 tagged VID 10 and lan2 untagged PVID 10. Ping between the peers. Then `ip link set br-lan type bridge vlan_filtering 0` and remove the VID 10 settings. | Frames cross the CPU with the right tag on each side; no driver VLAN debug line (the VA checks cover the offloaded case). |
| V3 | Send VID 1 tagged and priority-tagged (VID 0, PCP != 0) frames from a peer. | They reach the CPU still tagged, with VID and PCP unchanged. |
| V4 | 1522-byte tagged frames (`ping -s 1472` over a VLAN upper). | No drop or fragmentation over the CPU link. |
| V5 | Repeat the standalone-port negative forwarding check. | No user-to-user leak. |
| V6 | On the conduit capture, check head-tag bytes 16-17 (word 2) for untagged, priority-tagged and VID 10 frames from a user port. | `0000` in every case; the PPE head-tag alias depends on it. Then re-check PPE flow offload LAN->WAN and WAN->LAN. |

### Bridge offload checks

One bridge is offloaded: join links the port, through port isolation, with
the members in the same VLAN mode, the bridge flags drive learning and the
flood-mask bits, and leave restores the standalone state (CPU-only
isolation, no flooding or learning, flush, FORWARDING). The B, AL and U
checks use a `br-lan` without `bridge-vlan` sections (VLAN-unaware); the VA
checks below cover a VLAN-aware one. Follow phase 3 of the bench order, and
connect no physical loop before CF2, CF5 and a loop-free B7 have passed.
Debug lines as for the L checks. None of `bridge join failed`, `may still be
linked`, `held DISABLED`, `left unlinked`, `re-applied`, `mode unknown`,
`back to VLAN filtering`, `applied on retry`, `may keep a static entry` or
`entry not restored` is expected; record the dmesg around any of them.

| ID | Procedure | Pass condition |
| --- | --- | --- |
| B1 | Boot with br-lan = lan1–lan4 and offload on (phase 3), or re-enslave the ports after software mode (uppers deleted). `bridge link show`, dmesg. | `port N joined bridge 1, linked ...` for each port, the last to join listing the other three (0xe0 if it is port 4), and `bridge flags ... bridged 1` lines. No `bridge join failed` and no extack on enslave. T4 passes on each jack. |
| B2 | Hosts on lan1 and lan2: `iperf3` 30 s each way (and `-P 4`), at 2.5G if possible; `mpstat`/`top` on the router and `ip -s link show lan` before/after. Repeat T5's pairs. Ping between VID 10 uppers on the two hosts, capturing on both. Then address a `br-lan.10` on br-lan and ping/`iperf3` it from the lan2 host's VID 10 upper, capturing on the lan3 host. Last, V6's PPE check with br-lan offloaded: routed LAN->WAN and WAN->LAN `iperf3` from the lan1 host. | Near line rate with router CPU close to idle; the conduit counters barely move (LAN-to-LAN frames no longer cross the CPU). Compare with phase 1's software-mode T5 (or the Release-7 T5 results). The VID 10 ping between the hosts passes with the tag (VID, PCP) unchanged on both. Record whether the router-bound VID 10 unicast also reaches the lan3 host: expected, since it misses the table (README limitation). For the routed flows record throughput, router CPU, `ip -s link show lan` and word 2 of router-bound frames on the conduit (`0000`), and compare with V6: router-bound unicast now relies on the static CPU entries. Per-port counters (`ip -s link show lanN`) miss the hardware-forwarded traffic (no MIB counters). |
| B3 | `ip link set lan4 nomaster`, address it on its own subnet, then ARP/ping between the lan4 host and the lan1 host's address, with captures on both hosts. From the lan4 host also send unicast to the lan1 host's MAC (a static neighbour entry or `mausezahn`), mDNS and UDP to 239.1.1.1. Re-add lan4. Also put lan4 alone in a second bridge. | `port 4 left bridge 1`. The lan1 host sees no frame from the lan4 host and vice versa, multicast included; the unicast reaches the router only (`tcpdump -i lan4 -e`: a hit outside isolation is handled as unknown, `0x5350` bit 8). Multicast crossing over points at `0x508c`, left at reset. lan4 still reaches the router (T4). The second bridge stays in software: the enslave prints `Range of offloadable bridges exceeded` (B4), no join line appears, and lan4 stays CPU-only. Re-adding restores B1/B2. |
| B4 | Enslave a port to a second bridge (VLAN-aware or not) while br-lan is offloaded. Then reverse the order: `ifdown lan`, bring up the second bridge's interface, `ifup lan`; restore the order afterwards. | It enslaves with `Range of offloadable bridges exceeded` and forwards in software; its port stays CPU-only (B3). Reversed, the second bridge holds the slot: br-lan's ports log no `joined bridge` line (netifd shows no extack, see section 4) and their LAN-to-LAN traffic moves the conduit counters. |
| B5 | From the lan1 host send unknown unicast (static ARP for an absent MAC), unknown IPv4 multicast (`iperf3 -u` to 239.1.1.1, or mDNS), IPv6 multicast (to ff0e::1234), non-IP multicast (DA 03:00:00:00:00:01, custom ethertype) and broadcast (ARP); capture on lan2's host. Toggle `bridge link set dev lan2 flood off`, `mcast_flood off`, `bcast_flood off`, then back on. Try `hairpin on` and `isolated on`. | Each class stops reaching lan2 exactly when its flag is off and returns when on (all three multicast kinds with `mcast_flood`); the router (CPU) keeps seeing all of them. `hairpin`/`isolated` are refused. No frame is ever echoed back to its sender (user-port bits of `0x4f84` cleared at setup). |
| B6 | Hosts talking on lan1 and lan2; `bridge fdb show dev lan2`. Unplug lan2's cable, wait, show again; replug. | The lan2 host is a learned `self` entry; on unplug `port 6: STP state 0`, `port 6: dynamic L2 entries flushed (cmd 0x40)` and the entry is gone; it is learned again after replug. Static entries (L4) survive. |
| B7 | `ip link set br-lan type bridge stp_state 1`, then re-enslave a port; capture BPDUs on its peer. Then L4 with traffic to the static MAC. | CIST lines 1 (listening/blocking), 2, then 3 after 2 × forward_delay; no LAN-to-LAN traffic before 3, BPDUs reach the peer in every state. Traffic to the static MAC leaves only lan1, and router-bound unicast from lan1 is not flooded to lan2. |
| B8 | Remove every port from br-lan, one at a time, then re-add; also with static entries on active hosts (L4). | `port N left bridge 1` each time, no error line (no delete error either); then the standalone negative forwarding check and T4 pass on each removed port. |
| B9 | Same port: two hosts behind an unmanaged switch on lan2 exchange unicast (`iperf3`). | No copies on the lan1 host or the conduit once both are learned; nothing echoed back. A bridged port's isolation mask lists the port itself (as in the SDK-based driver), so the hit is inside isolation and the source-port filter (`0x4f84` user-port bits 0, CF1) drops the frame. Copies elsewhere, or frames echoed back to the lan2 hosts, mean `0x4f84` does not act as the SDK says: record them. |
| B10 | Shared VID 0/1 entry (VLAN-unaware br-lan): `ip link set br-lan type bridge vlan_default_pvid 0`, then `1`. Then `bridge fdb add 02:00:00:00:00:01 dev lan1 master static`, `bridge fdb replace ... dev lan2 master static vlan 1` (`add` would fail with EEXIST), `bridge fdb del ... dev lan2 master vlan 1` and `bridge fdb del ... dev lan1 master`. | `DSA vid 1 released by port 3, entry for port 3` for the br-lan MAC, and router-bound unicast from the lan1 host still not seen at the lan2 host; after `1`, B7's router-bound check passes. Then `static entry for port 7` once: VID 1 is inert while br-lan is VLAN-unaware (the bridge looks up VID 0), so the replace onto lan2 writes nothing; and the entry goes with the last delete (`bridge fdb show` no longer lists it as `self`). |
| B11 | Table fill: from the lan1 host send one broadcast from each of 300 random source MACs (`mausezahn <if> -a rand -b bcast -c 300`), then at once `bridge fdb show dev lan1 \| grep -c self`. Then `time bridge fdb show dev lan2` while adding and deleting a static entry on lan3 and flapping lan4. | No `L2 walk from ... stopped at status ... (bit 13)` warning expected; record any with its words. Without one, a count near 300 means the walk reaches every entry. The add/delete and the flush complete during the dump (debug timestamps); record the dump time and any `failed to add ... -28` (bucket full). |
| B12 | CIST DISABLED with the link up: br-lan with STP off, hosts on lan1 and lan2 sending to each other (ping, `iperf3 -u`); `bridge link set dev lan2 state 0` with the cable in, then `state 3`. | Debug `port 6: STP state 0, CIST 0`. While it holds, no data frame crosses in either direction and none from the lan2 host reaches the conduit (trapped link-local frames may: record them); `state 3` restores traffic at once. Frames crossing mean CIST 0 does not stop data with the link up, which the DISABLED hold of a port that could not be unlinked, and the DISABLED written at join, rely on. |
| AL1 | Assisted learning: br-lan with lan1–lan4 and the router's Wi-Fi, a Wi-Fi client W. `iperf3` 30 s from the lan1 host to W, both directions, with a promiscuous capture on the lan3 host (`ether host <W> and not broadcast and not multicast`). With 30+ Wi-Fi clients, count the distinct MACs in `failed to add` lines. | `L2 <W> vid 1: static entry for port 3` once W talks; `bridge fdb show \| grep <W>` lists W on the Wi-Fi interface with `offload`. No frame of the stream on lan3; B2's numbers unchanged. Record any `failed to add ... -28` (bucket full: that address floods). DSA tries a foreign address once per offloaded LAN port, so one that does not fit logs `port N failed to add <mac> vid 0 to fdb: -28` for each offloaded LAN port (4–7 with lan1–lan4), and when the bridge later ages or moves it, `port N failed to delete <mac> vid 0 from fdb: -2` for each (DSA kept no record): expected, not a driver delete failure. |
| AL2 | Roam: a Wi-Fi client of an AP on lan2, continuous ping to it from the lan1 host. Move it to the router's Wi-Fi (same MAC; lan2's link stays up), then back to the AP. | `static entry for port 3` right after it associates, and the ping resumes within seconds, not after the 300 s ageing. Back on the AP the ping keeps flowing, `DSA vid 0 released by port 3` follows its first broadcast or router-bound frame, and `bridge fdb show dev lan2` lists it as a learned `self` entry again. No `failed to add` or `failed to delete` for the client's MAC (lines for other MACs, such as AL1 addresses that did not fit, can appear). Then toggle-then-roam: with the client on the router's Wi-Fi, `ip link set br-lan type bridge vlan_filtering 1` (with VLAN 1 on the ports and br-lan), let it send a frame, move it to the AP on lan2 within 300 s and ping it from the lan1 host: the ping must not stall until the old VID-0 entry ages out. |
| U1 | lan1 in the offloaded br-lan: `ip link add link lan1 name lan1.100 type vlan id 100`. | Fails with EBUSY and `8021q uppers are not supported on ports of the offloaded bridge`; nothing changes in the switch. |
| U2 | lan1 out of br-lan, create lan1.100, add lan1 to br-lan. Send VID 100 broadcast and unknown unicast from lan1's host, with br-lan VLAN-unaware and again VLAN-aware. | The enslave succeeds with `Port has 8021q uppers, bridged in software` and no `port 7 joined bridge`; lan1's LAN-to-LAN traffic moves the conduit counters. The VID 100 frames reach lan1.100 and nothing at the lan2 host (`tcpdump -e`). Unicast from the lan2 host to the lan1 host also reaches the lan3 host: the switch gets no entry for the stations of a port bridged in software (README limitation); record it. |
| U3 | Delete lan1.100, remove lan1 from br-lan and add it again. If the bench config defines `lanN.M` devices, check its boot log for EBUSY from netifd. | lan1 is offloaded again (join line, B2 numbers). |

### VLAN-aware bridge checks

A VLAN-filtering bridge is offloaded: bridge VLANs become table entries, a
filtering port gets the ingress filter, the bridge PVID (tagged-only and
PVID 0 without one) and untagging per VLAN (egress ORIGINAL), and loses its
egress-membership exemption; VLAN 1 keeps the CPU and every VLAN-unaware
port. None of this has run on hardware. Run VA1, then VA9 (a gate before
relying on a port without a PVID), then CF5's VLAN-aware cases and the others. Suggested `bridge-vlan` setup on
`br-lan` (vlan_filtering 1): lan1 is a trunk to an AP (VID 1 untagged PVID,
VIDs 10 and 20 tagged), lan2 an access port of VID 10, lan3 of VID 20, lan4
of VID 1 (each untagged PVID); br-lan itself VID 1 untagged and VIDs 10/20
tagged, with `br-lan.10`/`br-lan.20` addressed. Debug lines (`VLAN N entry`,
`VLAN filtering`, `VLAN N added/deleted`, `bridge PVID`) need the debug image.
Capture on the conduit with `tcpdump -i lan -e -xx` as in the V checks.
Besides the lines listed for the B checks, `delete failed`, `stale` and
`not restored` are not expected.

| ID | Procedure | Pass condition |
| --- | --- | --- |
| VA1 | Boot with the setup above; `bridge vlan show`, dmesg. | `port N joined bridge 1, linked 0` per port (VLAN-unaware at join), then `port N: VLAN filtering 1, bridge PVID P, linked ...`, the last listing the other three, and per-port `VLAN N added` lines matching `bridge vlan show`. Last entry words: `VLAN 1 entry 0x02026098` (CPU, lan1, lan4; lan2/lan3 dropped), `VLAN 10 entry 0x020100c8` (CPU and lan1 tagged, lan2 untagged), `VLAN 20 entry 0x020080a8`. No `failed`, `not restored` or readback-mismatch line. |
| VA2 | Trunk: from the AP send VID 10 and VID 20 tagged traffic to hosts on lan2 and lan3, and back; `iperf3` both ways with `mpstat` on the router. | Frames reach lan2/lan3 untagged and the AP tagged with the right VID (PCP kept); hardware-forwarded (router CPU near idle, conduit counters still). |
| VA3 | Isolation: hosts on lan2 and lan3 on one subnet, ARP/ping between them; tagged VID 20 into lan2; VID 30 (configured nowhere) tagged into lan1; priority-tagged (VID 0, PCP 5) and VID 4095 tagged frames into lan2. Warm variant: VID 30 untagged on lan2 and lan3, then removed from the config, warm reboot, VID 30 tagged into lan2. | Nothing crosses between VID 10 and VID 20 except via the router; VID 20 on lan2 and VID 30 on lan1 (and on lan2 after the warm reboot) are dropped (seen on no port, not on the conduit). Priority-tagged frames from lan2 go to VID 10 like untagged ones and reach the conduit still VID-0 tagged; dropped means VID0_TYPE (`0x4e14` bit 0, pinned 0) works the other way round. VID 4095 is dropped; out in VID 10 means VID4095_TYPE (bit 1, pinned 1) does. |
| VA4 | CPU: hosts ping `br-lan.10`/`br-lan.20` addresses, the AP pings over VID 10 tagged; conduit capture. | Frames from access ports reach the conduit untagged, from the trunk tagged (no extra tag behind the head tag). Router frames leave access ports **untagged** and the trunk tagged. A VID 1 tag on a router frame at lan2/lan3 means the CPU tag's KEEP bit does not override ORIGINAL: stop and report, with the `VLAN egress keep ports` lines (fallback: keep br-lan VLAN-unaware, or the VLAN-aware bridge in software: software mode or `offload=0`). |
| VA5 | `bridge vlan add dev lan2 vid 10` (no `pvid`), then send untagged, priority-tagged and VID 10 tagged frames from lan2's host; then restore `pvid untagged`. | Debug `bridge PVID 0` (TAG_ONLY, PVID 0). Untagged and priority-tagged frames are dropped (on no port, not at the CPU, not in VLAN 1 at lan4); tagged VID 10 still passes. Restoring the PVID restores untagged traffic. |
| VA6 | Toggle at runtime: `ip link set br-lan type bridge vlan_filtering 0`, test, then `1`, test; repeat three times. Run VA2/VA3 under 1 and B2 under 0. Then, with the AP on lan1 sending VID 20 broadcast and unknown unicast, toggle ten times while capturing (`tcpdump -e`) on the lan2 host. | Every toggle logs `VLAN filtering 0/1` per port without errors, with `linked` masks growing port by port; LAN-to-LAN traffic between toggled and untoggled ports pauses until the last port toggles. Under 0 every port is one domain with tags passing unchanged, under 1 VA2–VA4 hold again. While br-lan is at vlan_filtering 1, and during each toggle while lan1 and lan2 are in different modes, the lan2 host sees no VID 20 frame; it never sees an untagged copy of one. Tagged VID 20 frames at lan2 are expected while the whole bridge is at 0. |
| VA7 | `ip link set lan2 nomaster`, then re-add it; also delete br-lan with all ports in it. | `port 6 left bridge 1`, `VLAN filtering 0`, the `VLAN 1 entry` includes the port again; the standalone negative check, T4 and V1 pass on it. Re-adding restores VA1. |
| VA8 | A VID present only on switch ports (`bridge vlan add dev lan2 vid 40`, same on lan3, not on br-lan): traffic between their hosts on VID 40; capture on the conduit. | Forwarded lan2↔lan3, but no VID 40 copies reach the CPU. |
| VA9 | Gate: STP on in br-lan; from lan2's peer send untagged BPDUs, LLDPDUs (`lldpd`) and EAPOL-Start, first with lan2 at PVID 10, then with lan2 a tagged-only trunk (VIDs 10 and 20 tagged, no PVID). | They reach the CPU (RMA trap, VLAN-leaky) and no other port in both states. If they are lost without a PVID, TAG_ONLY admission acts before the trap: report it and give such ports a PVID; the fix is a driver change (accept all, PVID 0). |
| VA10 | PPE: a routed LAN->WAN flow from lan4's host while br-lan is VLAN-aware; then toggle vlan_filtering with an offloaded flow live. | Offload towards ports of the VLAN-aware bridge is declined (0449); after the toggle no frame leaves a LAN port with a leaked VID 1 tag. |
| VA11 | Join under load: lan3 access VID 20, lan2 out of br-lan with its host flooding VID-20-tagged frames; add lan2 to br-lan. | Nothing reaches the lan3 host at any time (lan2 has no VID 20). |
| U4 | `ip link add link lan2 name lan2.10 type vlan id 10` on the setup above (VID 10 is lan2's PVID); then `bridge vlan show dev lan2` and untagged traffic from the lan2 host; then `bridge vlan del dev lan2 vid 10` and `bridge vlan add dev lan2 vid 10 pvid untagged`. | Refused (EBUSY, by DSA or the driver). DSA-core side effect (README): debug `port 6: VLAN 10 added (flags 0), bridge PVID 0`, then `port 6: VLAN 10 deleted`; the lan2 host is cut off while `bridge vlan show` still lists VID 10. The delete and re-add restore traffic. Record anything else. |
| U5 | Mixed VLAN-aware br-lan, on the setup above: lan3 out of br-lan, create lan3.99, add lan3 back (bridged in software, U2). Add VID 30 tagged on lan1 and untagged PVID on lan3, not on br-lan itself, and send VID 30 traffic both ways between the AP on lan1 and the lan3 host, capturing on both and on the conduit. Then delete lan3.99 and re-enslave lan3. | Expected with this build (README limitation): from the lan3 host to the AP it works, through the CPU; from the AP nothing reaches the lan3 host, nor the conduit (the CPU port is not in VLAN 30). Record both directions; VID 30 reaching the lan3 host means the limitation does not hold. |

### Do not run these as P0 tests

- Do not run `tests/bridge-flags.sh` as is: it aborts at its first `hairpin`
  readback (this driver refuses `hairpin` and `isolated`), before any
  forwarding check. B5 and L5 cover the supported flags.
- Do not test FDB/MDB or STP offload beyond the L, B, AL, U and VA checks, LAG (#47), rate
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
| br-lan forwards through the CPU although it should be offloaded | dmesg `hardware bridging disabled`, `/sys/module/rtl8372n_dsa/parameters/offload`, the extack of `ip link set lanN master br-lan` (netifd shows none), `ip link` uppers of each port, the order in which bridges came up | `offload=0` still in `/etc/modules.conf`, the setup gate (TE1), the single slot taken by another bridge (B4), an 8021q upper on the port (U2). |
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
Debug image confirmed (T0/T1) / offload parameter per phase:
Bench phase reached (section 4) / first failing check:
"hardware bridging disabled" reason, or none:
TE / CF / L / V / B / AL / U / VA results:
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
