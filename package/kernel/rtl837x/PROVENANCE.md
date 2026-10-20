# RTL8372N P0 source and operation ledger

This ledger records the actual public inputs and their use. Public availability,
repository licensing and matching register values do not establish independent
origin by themselves. The candidate excludes the restricted SDK header and
PHY/SerDes patch payloads; review of the remaining lineage is still pending.

## Pinned inputs

| Input | Revision / declared license | Use and limit |
| --- | --- | --- |
| [Airjinkela DSA](https://github.com/airjinkela/rtl837x-dsa-driver/tree/7f3b64c7f3a37db1866fd80ba815d89974d995fd) | `7f3b64c7f3a37db1866fd80ba815d89974d995fd`, GPL-2.0-or-later source headers | Adapted DSA/phylink/MDIO architecture and retained author attribution. Its restricted generated header and patch arrays are excluded. The author's SDK lineage disclosure remains relevant to retained definitions. |
| [RTLPlayground](https://github.com/logicog/RTLPlayground/tree/f0aea3dcac056e3274fd39e1c76a7117471c37da) | `f0aea3dcac056e3274fd39e1c76a7117471c37da`, MIT repository notice | Register/operation comparison; `NOTICE` retains the MIT copyright/permission notice. This is a public corroborating source, not proof of independent research. |
| [ZTE RTL8372N source](https://github.com/cnjn/linux-mainline-zte-zxslc-sr1010/blob/07f8687248578d4be6931c665ff5d08bb6cc3d9d/drivers/net/ethernet/zte/zx279133-rtl8372n.c) | `07f8687248578d4be6931c665ff5d08bb6cc3d9d`, GPL-2.0 source metadata | Compare register facts and operations. Its SDS transaction helper corroborates the indirect command layout; its CPU-port-8 mode, reset, patch and polarity sequence is board-specific and is not imported. Source-owner provenance still needs review. |
| [Linux Realtek PHY](https://github.com/torvalds/linux/blob/v6.18/drivers/net/phy/realtek/realtek_main.c) | `v6.18`, GPL-2.0+ | Published PHY page/capability/status behavior. Original file credits Johnson Leung / Freescale Semiconductor (2004); see its retained upstream attribution and history. Candidate native MMD access uses the actual PHY port, unlike this source's single-PHY address-0 VEND2 helper. |
| [Linux r8169 transport](https://github.com/torvalds/linux/blob/v6.18/drivers/net/ethernet/realtek/r8169_main.c) | `v6.18`, GPL-2.0-only | Published `r8168g_mdio_read/write` page/OCP address semantics. Source credits ShuChen / Realtek (2002), Francois Romieu (2003–2007) and other contributors. No NIC MMIO transport or firmware is imported. |
| [Linux MDIO definitions](https://github.com/torvalds/linux/blob/v6.18/include/uapi/linux/mdio.h) | `v6.18`, GPL-2.0 WITH Linux-syscall-note | Standard MMD/2.5G advertisement constants, used through the kernel header. |
| [Linux MDIO bus matching](https://github.com/gregkh/linux/blob/v6.18.39/drivers/net/phy/mdio_bus.c), [bus registration](https://github.com/gregkh/linux/blob/v6.18.39/drivers/net/phy/mdio_bus_provider.c), [OF registration](https://github.com/gregkh/linux/blob/v6.18.39/drivers/net/mdio/of_mdio.c) and [fwnode helper](https://github.com/gregkh/linux/blob/v6.18.39/drivers/net/mdio/fwnode_mdio.c) | `v6.18.39`, upstream kernel sources | Use public `get_phy_device` (C45), `phy_device_register`, OF association and managed bus teardown. New registration orchestration retains real hardware IDs; no vendor PHY data imported. |
| [Linux PHY device lifecycle](https://github.com/gregkh/linux/blob/v6.18.39/drivers/net/phy/phy_device.c) and [driver core](https://github.com/gregkh/linux/blob/v6.18.39/drivers/base/dd.c) | `v6.18.39`, upstream kernel sources | Review `get_phy_c45_ids`, `phy_bus_match`, `phy_probe` and `device_is_bound`; use the exported binding API under the device lock. Diagnostic logging is new code; no PHY IDs or vendor payloads are introduced. |
| [MNeroba/openwrt-flint3#1](https://github.com/MNeroba/openwrt-flint3/pull/1) (Maksims Neroba, "serialized VLAN table foundation", P1-A) | head `b6c2da893e1d7a6427dab737e1c56e21cccf82ab`, GPL-2.0-only | Adapted in `rtl837x_table.c`: table mutex, idle wait, single command write, verified VLAN write. |
| [MNeroba/openwrt-flint3#2](https://github.com/MNeroba/openwrt-flint3/pull/2) (Maksims Neroba, "STP and BPDU test hooks", P1-B) | head `847e9f79039b58ed6674502b64f6f2b2b24a251a`, GPL-2.0-only | Adapted: the L2 key lookup and hit check, the per-port flush (`rtl837x_l2_flush_port()`), the CIST helper and the bridge-to-CIST mapping (`rtl837x_table.c`, `rtl8372n_l2.c`). Its bench results (release 12) are hardware evidence below. Its static VID-1 BPDU entry is not used. |
| [airjinkela/rtl837x-dsa-driver#2](https://github.com/airjinkela/rtl837x-dsa-driver/pull/2) (Maksims Neroba, "trap BPDUs with native CPU tagging", closed) | head `15ff009cb46c27a443d1f1334237807b212bf9c4`, GPL-2.0-or-later repository | No code: its RMA/PTP trap-destination analysis (`0x4f34` as the shared RMA/PTP CPU select, an inference from the SDK name) and airjinkela's hardware comment on it (trapped frames reach the CPU port) informed the trap setup. |
| Linux v7.2 `rtl8365mb`/`rtl83xx` bridge support, as OpenWrt backports `target/linux/generic/backport-6.18/942-05` to `942-09-7.2-*` | upstream `fbafdd3b22`, `9da2c8672f`, `336e3e4a1a`, `183bd68b1f`, `660a9e399a`; `rtl8365mb_*.c` GPL-2.0, `rtl83xx.c` GPL-2.0+ | Followed, not copied: `rtl8365mb_table_query()` (shape of the table transaction), `rtl8365mb_port_vlan_filtering/add/del` and `rtl8365mb_vlan_pvid_port_clear` (VLAN callbacks, no-PVID handling), `rtl83xx_port_bridge_join/leave` (join/leave shape), the FDB and bridge-flag callbacks, and `assisted_learning_on_cpu_port`. Not in the pinned v6.18 tree. |
| [Linux v6.18.39 DSA and bridge core](https://github.com/gregkh/linux/tree/v6.18.39/net/dsa), `sja1105_prechangeupper()` and `tag_brcm` | `v6.18.39`, upstream kernel sources; `sja1105_main.c` GPL-2.0, `tag_brcm.c`/`tag_mxl862xx.c` GPL-2.0+ | DSA/bridge call order checked against `net/dsa/{port,user,switch}.c`, `net/bridge/br_{vlan,fdb,if}.c` and `net/8021q/vlan.c`. Patterns, written anew: the 8021q-upper refusal (sja1105) and the link-local rule of the `0457` tag_rtl8_4 patch (`tag_brcm`, `tag_mxl862xx`). |
| RTL8373 SDK (`src/rtk-api`: `rtl8373_reg_definition.h`, `dal_rtl8373_*.c`) and the SDK-based driver (`rtl837x_dsa_ops.c` and siblings) formerly in this package | `perceival/openwrt-flint3` `2365932733ca8ec3b346621d9cec2eb3df3b2cf3`, `package/kernel/rtl837x/src/`; the SDK files carry a Realtek proprietary notice ("All Rights Reserved"), the driver files a GPL-2.0 header (Air) | Comparison only; no code or header imported. Restricted lineage: the source of every row below that cites "RTL8373 SDK" or "the SDK-based driver", and of every **SDK-only lineage** disposition. |

## Code reuse and copyright-risk review

- The P0 implementation does not include Airjinkela's restricted generated
  register header, `rtk-api`, or the PHY/SerDes patch payloads. The module's
  object list is limited to the files listed below.
- `rtl837x_table.c` adapts Maksims Neroba's table layer from
  MNeroba/openwrt-flint3#1 (`b6c2da89`, table mutex, idle wait, single
  command write, verified VLAN write) and his L2 key lookup and per-port
  flush (`rtl837x_l2_flush_port()`, BUSY-only completion, mode restored only
  when idle) from #2 (`847e9f79`), both GPL-2.0-only, and follows the shape
  of Linux v7.2 `rtl8365mb_table_query()` (backport 942-05, GPL-2.0). It
  therefore declares GPL-2.0-only.
- `rtl8372n_l2.c` adapts the CIST state helper and encoding from Maksims
  Neroba's `rtl837x_stp.c`, and the bridge-to-CIST state mapping of his STP
  callback, in MNeroba/openwrt-flint3#2 (`847e9f79`, GPL-2.0-only) and
  therefore also declares GPL-2.0-only. Its FDB alias record and the
  callbacks around it are new code.
- `rtl8372n_vlan.c` (VLAN shadow, per-port VLAN mode, VLAN add/delete, the
  VLAN table reset) follows Linux v7.2 `rtl8365mb_port_vlan_filtering/add/del`
  (backport 942-06, GPL-2.0) and the SDK-based driver's PVID-per-mode rule
  (`rtl837x_commit_pvid_for_mode()`), and writes entries through the table
  layer above (MNeroba#1). It declares GPL-2.0-only.
- The bridge join/leave and isolation code in `rtl8372n.c` follows Linux
  v7.2 `rtl83xx_port_bridge_join/leave` (backport 942-08, GPL-2.0+) in
  shape; the same-VLAN-mode linking, the DISABLED hold, the offload gate and
  the 8021q-upper refusal (the sja1105 pattern) are new code. The `0457`
  kernel patch to `tag_rtl8_4.c` is new code following `tag_brcm`.
- Air-derived portions remain in the candidate. The original Air copyright
  notice is retained in `rtl8372n.c`, `rtl837x_mdio.c` and `rtl837x.h`; the
  shared SDS/register helpers in `rtl837x_common.c` are substantially
  reworked and now carry an explicit “Portions adapted” notice. These files
  declare GPL-2.0-or-later, matching the Air source headers.
- The internal PHYs are driven by the kernel's Realtek PHY driver
  (`RTL8224 2.5Gbps PHY`); the former private PHY layer `rtl8372n_phy.c` and
  its C22 page emulation are removed. The RTLPlayground MIT notice is retained
  in [NOTICE](NOTICE). ZTE is used for
  comparison only; no ZTE implementation block is included.
- This reduces the risk of uncredited copying, but it does not establish that
  Air's retained switch-specific work is independently sourced. The author
  disclosed SDK-derived lineage without identifying exactly which functions
  or definitions were affected. GPL headers and public repository licenses do
  not resolve rights in any third-party SDK-derived material. Keep the source
  owner/maintainer review open and do not describe the candidate as
  provenance-cleared until that lineage has a defensible disposition.

## Feature-to-source map

Locations name functions/symbols in the pinned files to keep references stable
through candidate edits. Hardware qualification remains incomplete for the
register and feature rows. Release 7 passes T0/T1/T2/T3/T4/T5/T7 and the full
six-pair T5 software-bridge matrix. The [Release-7 follow-up](https://github.com/perceival/openwrt-flint3/pull/104#issuecomment-6044579250)
provides kernel link events confirming physical cycles on all four jacks,
full T2 topology output and the 100-packet T4 check. Release 8 on source commit `7e51247b` reports 33/33 matching setup
readbacks and one standalone-port CPU-only isolation case; reverse-direction
evidence is counter-only. Broader register-level and feature qualification
remains open. See the [Release-8 report](https://github.com/perceival/openwrt-flint3/pull/104#issuecomment-6048213461)
and [raw logs](https://gist.github.com/perceival/3172215cb83c6ea3866fd55bee0808be).
See [the release-7 report](https://github.com/perceival/openwrt-flint3/pull/104#issuecomment-6037094988)
and [raw logs](https://gist.github.com/perceival/145d80ee322c48e88870ec011ae1200d).

| Candidate feature/symbols | Specific public source location | Derivation / remaining question |
| --- | --- | --- |
| MDIO registers 21–24, commands `0x19/0x1b`, BUSY bit | ZTE `RTL8372N_SMI_*`, `rtl8372n_wait_ready/read_reg/write_reg`; Air `src/rtl837x_mdio.c` | Adapted 32-bit register transport; new reset writer omits post-command polling. Reset semantics need hardware confirmation. |
| Chip ID `0x0004`, reset `0x0024`, accepted `0x837270xx` | RTLPlayground `rtl837x_regs.h`; ZTE ID/reset definitions and detection | Compared register facts; exact revision/reset defaults need readback. |
| PHY indirect `0x6438`–`0x6444`, status bits 24–26, C45 command encoding | ZTE `rtl8372n_phy_read/write`; RTLPlayground PHY transport routines | Reimplemented serialized access. Hardware polling arbitration and timeout budget need confirmation. |
| PHY power OCP `0xa610`, `0x2058/0x2858` | RTLPlayground `rtl837x_port.c` / `rtl837x_init.c` power operations; Air PHY enable/disable | Public register/value comparison. Whole-register writes and revision dependence remain unverified. |
| Internal PHY binding: C45 registration, RTL8224 driver | Linux `get_phy_device(..., true)`, Realtek `realtek_main.c` RTL8224 entry (ID `0x001ccad0`) | Upstream driver; no PHY register knowledge is carried in this package. The SDK-based driver gets the same binding on BE9300. |
| SMI polling/MAC type `0x632c/0x6330/0x6334/0x6454` | RTLPlayground `rtl837x_regs.h`, init; ZTE setup | Separate contiguous fields. Reset defaults and automatic MAC tracking must be checked. |
| MAC status `0x63e8` link (bit per port), speed `0x63f0`/`0x63f4` (4 bits per port, 4 = 10G), duplex `0x63f8`, TX/RX pause `0x63fc`/`0x6400` | RTL8373 SDK `RTL8373_MAC_LINK_STS_ADDR`, `MAC_LINK_SPD_STS_ADDR(port)`, `MAC_LINK_DUP_STS_ADDR`, TX/RX pause status, `dal_rtl8373_portStatus_get()` (the SDK-based driver's `rtk_port_macStatus_get()`); no pinned public source located in this review | **SDK-only lineage, disposition open** unless Air, RTLPlayground or ZTE is found to carry them. Read only, by `rtl8372n_sds_pcs_get_state()` for the 10GBASE-R SerDes ports 3 and 8, since P0 (`a265245d79`); no value has been recorded on hardware (the CPU port's DT fixed-link carrier is no evidence for it). |
| SDS indirect `0x03f8/0x03fc/0x0400`, command fields | [Air access helper](https://github.com/airjinkela/rtl837x-dsa-driver/blob/7f3b64c7f3a37db1866fd80ba815d89974d995fd/src/rtl837x_common.c#L408-L472) and [generated definitions](https://github.com/airjinkela/rtl837x-dsa-driver/blob/7f3b64c7f3a37db1866fd80ba815d89974d995fd/src/rtl8373_reg_definition.h#L2967-L2985); [ZTE helper](https://github.com/cnjn/linux-mainline-zte-zxslc-sr1010/blob/07f8687248578d4be6931c665ff5d08bb6cc3d9d/drivers/net/ethernet/zte/zx279133-rtl8372n.c#L1761-L1845); [RTLPlayground register map](https://github.com/logicog/RTLPlayground/blob/f0aea3dcac056e3274fd39e1c76a7117471c37da/rtl837x_regs.h#L64-L88) and SDS operations | The address and command layout agree technically across the references. Air's author disclosed SDK-derived lineage; the ZTE lineage is unreviewed; RTLPlayground declares MIT, but the reviewed material does not establish independent derivation. No vendor header or patch array is imported. Resolve source lineage separately from hardware behavior. |
| SDS mode `0x7b20`, mode `0x1a`, polarity pages 0/6 and bits | RTLPlayground [mode constants](https://github.com/logicog/RTLPlayground/blob/f0aea3dcac056e3274fd39e1c76a7117471c37da/rtl837x_regs.h#L64-L88), [SDS init](https://github.com/logicog/RTLPlayground/blob/f0aea3dcac056e3274fd39e1c76a7117471c37da/rtl837x_init.c#L21-L90), [OEM companion settings](https://github.com/logicog/RTLPlayground/blob/f0aea3dcac056e3274fd39e1c76a7117471c37da/machine_init.c#L60-L77); Air [interface-to-mode mapping](https://github.com/airjinkela/rtl837x-dsa-driver/blob/7f3b64c7f3a37db1866fd80ba815d89974d995fd/src/rtl837x_common.c#L207-L220); ZTE [CPU-port-8 setup](https://github.com/cnjn/linux-mainline-zte-zxslc-sr1010/blob/07f8687248578d4be6931c665ff5d08bb6cc3d9d/drivers/net/ethernet/zte/zx279133-rtl8372n.c#L1986-L2040) | `0x1a` is identified as 10GBASE-R/10GR by Air and RTLPlayground. ZTE's value `0x0d` is used with a different USXGMII CPU-port-8 setup and is not a conflicting Flint3 setting. RTLPlayground documents RX swap at page 0/reg 0 bit 9 and page 6/reg 2 bit 13. For TX, RTLPlayground says OEM firmware sets companion SDS0 polarity bits at page 0 bit 8 and page 6 bit 14 for RTL8221B; ZTE also changes these bits in a board-specific sequence. This corroborates behavior but is not an independent clean source. The candidate applies these settings to both SDS lanes from board properties; lane mapping and each flag's effect have not been independently verified. Release-7 link/traffic tests exercise the board configuration as a whole, not each polarity field independently. Generic reset/patch sequences remain excluded; post-reset and polarity-specific coverage are still open. |
| RTL8_4 CPU tag `0x603c/0x6720/0x6724` | RTLPlayground `rtl837x_regs.h` CPU tag symbols; Air `set_tag_rtl`; Linux `net/dsa/tag_rtl8_4.c`; RTL8373 SDK `CPU_TAG_AWARE_CTRL` (bits 9:0, bit 9 the 8051) | Native tagger; no copied tagger or private 802.1Q protocol. The tag-aware mask of ports 0–8 is written whole to the CPU port alone (P0 ORed the CPU bit into the reset value; the SDK does too) and read back; bit 9 is left as read. Header format, forwarding reasons and port identity need traffic tests. |
| VLAN 1, table `0x5cac/0x5cb8`, read data `0x5ccc`, PVID `0x4e1c`, filter `0x4e14/0x4e18` | RTLPlayground [`vlan_setup` / `vlan_get`](https://github.com/logicog/RTLPlayground/blob/f0aea3dcac056e3274fd39e1c76a7117471c37da/rtl837x_port.c); ZTE VLAN map | Global filter `0x4e14` stays on, as in Linux rtl8365mb; per-port ingress filter `0x4e18` is off on VLAN-unaware ports and on in a VLAN-aware bridge. VLAN 1 is written through the table engine below and read back; at setup the word (`0x0203e0f8` on BE9300) is unchanged from P0. The DSA VLAN callbacks write whole entries from a driver shadow; VLAN 1 is the bridge's VLAN 1 plus the CPU and every VLAN-unaware port and is never removed. Only VLAN 1 has been written on hardware (Release 8, MNeroba#2). |
| Table engine: `0x5cac` address 28:16, type 10:8 (3 VLAN, 4 L2), write bit 1, execute/busy bit 0; data `0x5cb8 + 4i` / `0x5ccc + 4i`; `0x5cb0` method 17:14, ENTRY_CLR 18, hit 12, index 11:0 | RTLPlayground `rtl837x_regs.h` table section, `port_l2_learned`, `doc/l2.md`; ZTE `RTL8372N_L2_*` method/hit/clear/index fields; MNeroba#1/#2 | Hardware: VLAN write/read (P0, #2) and an L2 multicast write plus method-0 lookup (#2). The 13-bit address and 3-bit type widths, and the method-7 port filter `0x5cb0` 22:19, are **SDK-only lineage, disposition open**. Every L2 operation now writes method, port and ENTRY_CLR explicitly; non-zero method and port values, next-entry walks and ACT_STS = 0 have never run. Bit 13 of `0x5cb0` (SDK TBL_TYPE, undocumented; perhaps an overflow-CAM hit) ends an FDB walk with a warning: **SDK-only lineage, meaning unknown** (B11). |
| VLAN entry bits 23:20 FID/MSTI, 24 SVLAN check, 25 IVL | Air `ivl_en`, ZTE `VLAN_IVL` (bit 25); RTL8373 SDK field list for 23:20 and 24 | Bit 25 is written as 1 in every entry the driver programs (VIDs 0, 1 and bridge VLANs, as P0 did), with bits 20–24 written 0, and each such entry is read back; RTLPlayground's prose calls bit 25 "valid" and the bench test is open. The setup sweep (VLAN table reset row) writes the all-zero word to VIDs 2–4095, which leaves them SVL with no members, as a table reset is expected to leave them, and reads back only the VID 2/4094/4095 samples. Bits 20–24 are **SDK-only lineage, disposition open**. FID/MSTI 0 is what makes the CIST register `0x5310` govern every VLAN; B7 tests it on VLAN 1, and every programmed entry carries the same field. |
| L2 unicast entry: MAC, VID/FID 27:16, IVL 29, L3 28, port 31:30 + word 2 1:0, age 4:2, auth 5, static 16 | ZTE `rtl8372n_l2_encode_key` / unicast program and delete; RTLPlayground L2 dump and static management entry; SDK `_rtl8373_fdbStUser2Smi` | Add writes age 6 and auth 1 (SDK, old driver, ZTE; RTLPlayground uses age 7, auth 0). Delete rewrites the key with no port, age or flags (SDK, ZTE). Used by the DSA FDB callbacks: static entries only; DSA VIDs 0 and 1 share the entry {MAC, VLAN 1}, kept by a per-MAC record while either is held; CPU-port entries for the bridge's own addresses and, with assisted learning, for addresses learned on foreign ports; delete only of a static entry of the same port (an entry learned again at once counts as deleted); dump by the method-7 walk on bridged ports only. **SDK-only**: a key write replaces the entry in place, learned or static, and learning never sets the static bit. No unicast entry has been written or walked on hardware. |
| L2 table clear `0x53d8` bit 0 (write 1, reads 1 while busy) | RTL8373 SDK `dal_rtl8373_l2_table_clear` / `_clearStatus_get`; the SDK-based driver wrote it at every setup without polling | **SDK-only lineage, disposition open.** Hardware-used by the old driver; the busy poll and its duration are untested. A poll timeout only turns bridge offload off (TE1). |
| VLAN admission `0x4e10` (2 bits/port, 0 = all, 1 = tagged only), egress tag mode `0x6738` (2 bits/port) | RTLPlayground `rtl837x_regs.h` `RTL837x_REG_INGRESS`, `rtl837x_port.c` `RTL837X_VLAN_PORT_EGR_TAG`; ZTE `RTL8372N_VLAN_ACCEPT_FRAME`, `RTL8372N_PORT_TAG_MODE` | Addresses and 2-bit per-port layouts are public. The tag-mode encoding (ORIGINAL 0, REAL_KEEP 3) follows Linux rtl8365mb `enum rtl8365mb_vlan_egress_mode` by analogy; the RTL8373 SDK formerly shipped in this package agrees. Admission 1 (TAG_ONLY, used on a VLAN-aware port without a bridge PVID) is **SDK-only lineage, disposition open** (`rtk_vlan_acceptFrameType_t`; rtl8365mb's TAGGED_ONLY by analogy) and has never run on this chip; whether it drops priority-tagged frames is unknown, and if it acts before the RMA trap, untagged link-local frames from such a port are lost (VA9). Needs readback and tagged-traffic tests. |
| VLAN egress transparency `0x4eb8 + (port / 3) * 4`, 10-bit ingress-port mask per egress port | No pinned public source. Only the RTL8373 SDK formerly shipped in this package (`RTL8373_VLAN_PORT_EGR_TRANS_*`, `dal_rtl8373_vlan_portTransparent_set`); the mechanism matches Linux rtl8365mb `rtl8365mb_port_set_transparent` | **SDK-only lineage, disposition open.** A VLAN-aware port's ingress bit is now cleared in every field (rtl8365mb); the CPU's stays set. Needs a maintainer decision, or an independent characterisation on the bench (tagged VID != 1 reaching the CPU with the field set vs. cleared). |
| VLAN-aware port mode: PVID `0x4e1c` = bridge PVID, or 0 with admission TAG_ONLY when the bridge gives none; VID 0 entry written empty at setup (`0x02000000`) as the sink for that PVID; PVID 1 whenever the port is VLAN-unaware | Linux v7.2 rtl8365mb (backport 942-06: `rtl8365mb_vlan_pvid_port_clear` writes VID 0, TAGGED_ONLY); the SDK-based driver's `rtl837x_commit_pvid_for_mode()` (bridge PVID only while filtering, 0 without one, 1 when unaware) | PVID 0 on a VLAN-unaware port gave a dead LAN under the SDK-based driver, so it is never written there; that is the only hardware evidence. The rest of the PVID-per-mode rule is an old-driver rule, deployed but never benched separately. PVID 0 with an empty VID 0 and the ingress filter on, and writing VID 0 at all, have never run on this chip; a failed VID 0 write turns bridge offload off. |
| Isolation `0x50c0 + port * 4` | RTLPlayground isolation register; ZTE isolation operations | CPU-only matrix at setup; unused ports cleared, now including the internal 8051 port 9 (`0x50e4`, and its learning limit `0x53a8`): the stride is hardware-proven for ports 0–8 (Release 8), port 9 itself is **SDK-only lineage**. Two user ports are linked only while both offload the bridge and are in the same hardware VLAN mode: a port is added to those members' masks, then its own mask opens to CPU + them + itself (the self bit, which every mask of the SDK-based driver had, `rtl837x_isolation_mask_for_port()`, with `0x4f84` cleared); unlinking closes its own mask first. Standalone ports stay CPU-only, without the self bit. A port whose unlink failed is held in CIST DISABLED. Join/leave follow Linux v7.2 `rtl83xx_port_bridge_join/leave` (backport 942-08) in shape; per-bridge member masks have not run on hardware (the SDK-based driver kept isolation open). |
| Learning limit `0x5384 + port * 4`, mask bits 12:0; 4160 = learning on | ZTE `RTL8372N_L2_LEARN_LIMIT_*` (including `_MAX` 4160), bridge-flags learning update; RTLPlayground limit address; RTL8373 SDK `dal_rtl8373_l2_limitLearningCnt_set` (maximum = LUT size 4160) | Zero limit at setup. `rtl8372n_port_set_learning()` writes 4160 or 0 (user ports only; the CPU port stays 0), from the `learning` bridge flag while the port is bridged, and 0 on leave. The SDK-based driver wrote 4160 on bridged ports and learned in hardware; that 0 stops learning and the limit-exceeded behavior are unmeasured. |
| Flood `0x5360`–`0x5370`, mask bits 9:0 | ZTE `RTL8372N_*FLOOD`, `rtl8372n_flood_port_set`; the SDK-based driver's bridge-flag mapping | CPU-only targets at setup; link-local control frames are trapped by the RMA row below. `rtl8372n_port_set_flood()` maps BR_FLOOD to `0x5360`, BR_MCAST_FLOOD to `0x5364`/`0x5368`/`0x536c`, BR_BCAST_FLOOD to `0x5370`, keeps the CPU bit; driven by the bridge flags while the port is bridged and cleared on leave. A failed enable rolls back; a failed disable still clears the other classes. |
| CIST state `0x5310`, two bits per port: 0 disabled, 1 blocking, 2 learning, 3 forwarding | RTLPlayground `0x5310` and STP states; MNeroba#2 `rtl837x_stp_set_state`; RTL8373 SDK `dal_rtl8373_asicMstpPortStatus_set` | Hardware: MNeroba#2 read back 0/1/1/2/3 on port 4 and an administrative down/up 0→3 on BE9300, and its release 12 captured a peer's BPDUs at the CPU-facing lan4 in states 1–3 (with its static BPDU entry and CPU-only isolation; path unresolved). Setup now writes FORWARDING on every available port and DISABLED on unused ports 0–2 and 8; port 9 is never written. The DSA STP callback then writes every DSA port (blocking and listening → 1, unknown → 1). That states 0–2 block data frames is unmeasured. |
| Per-port flush `0x53d4` (port mask 9:0, START 16, BUSY 17) and mode `0x53dc` (1:0 by port/VID/FID, 2 = also static) | RTLPlayground `port_l2_forget_port()` (`0x53dc` = 0 "port-based, dynamic entries", START bit 16 + port); ZTE `RTL8372N_L2_FLUSH_*`; MNeroba#2 `rtl837x_l2_flush_port()`; RTL8373 SDK `dal_rtl8373_l2_ucastAddr_flush` | Hardware: MNeroba#2 flushes on ports 4–7 completed polling BUSY only, read back the port mask (`0x80`/`0x40`/`0x20`/`0x10`) and mode bits 2:0 = 0. Used by fast-age with mode 0, restored only when idle. RTLPlayground and the SDK document that mode 0 removes learned entries and spares static ones. MNeroba#2 (release 12) saw a static L2 multicast entry for CPU port 3 survive a mode-0 flush of port 4, weak evidence since that entry was never a candidate; sparing a static unicast entry of the flushed port, and removing learned entries, are unmeasured (L5, B6). |
| Ageing `0x5354` bits 15:0 = seconds × 5, clamped to 14–800 s | ZTE `zx279133_rtl8372n_set_ageing_time` (same factor and clamp, "200 ms ticks"); RTL8373 SDK `dal_rtl8373_l2_ageout_timer_set`; the SDK-based driver | **SDK-only lineage, disposition open**: ZTE's code cites the SDK DAL for the factor. The clamp has no SDK basis (its comment says 45–458 s). The driver clamps instead of failing a join. Hardware-used at 300 s by the SDK-based driver; real ageing and its link to the entry age field are unmeasured. |
| Lookup-miss actions `0x5374` (unknown unicast), `0x4f74` (L2 multicast), `0x4f78`/`0x4f7c` (IPv4/IPv6 multicast); new-SA action `0x4f6c` and unmatched-SA action `0x4f70`; two bits per port, 0 = forward (flood mask) | RTL8373 SDK `dal_rtl8373_l2_unknUc_action_set`, `unknMc/unknV4Mc/unknV6Mc_action_set`, `portNewSaBehavior_set` (`L2_NEWSA_FWD` = 0), `portUnmatchedSaBehavior_set` | **SDK-only lineage, disposition open.** Written 0 on every available port at setup; reset values and effect never measured. SA-moving forbid `0x535c` (the SDK's polarity comment contradicts the register name) is left at reset and only logged (`SA-moving forbid ... (not checked)`, CF1). |
| ASIC IGMP/MLD snooping enable `0x5290` bit 0 | RTL8373 SDK `dal_rtl8373_igmp_state_set`; the SDK-based driver enabled it at probe | **SDK-only lineage, disposition open.** Written 0 at setup (no MDB offload); the reset value is unrecorded. |
| Link-local trap: RMA words `0x4ecc + 4 * slot` (slots 0, 2–7 = 01:80:c2:00:00:00, 02–0f; slot 1 PAUSE untouched) and LLDP `0x4f18`, fields action 5:4 (1 = trap), CKEEP 2, VLAN-leaky 1, isolation-leaky 0; RMA_CFG `0x4f1c` LLDP_EN bit 3; trap CPU selects `0x4f34` 17:16 and `0x5350` 16:15 (2 = external CPU) | RTLPlayground `RTL837X_RMA0_CONF` (`0x4ecc`) and `port_rldp_on()` (addresses only); RTL8373 SDK `dal_rtl8373_asicRma_set`, `asicRmaLldp_set`, `RTL8373_RMA_PTP_TRAP_CTRL`, `dal_rtl8373_l2_trapPort_set`; Maksims Neroba's RMA/PTP trap-destination analysis in airjinkela/rtl837x-dsa-driver#2 | **SDK-only lineage, disposition open** for every field; the SDK-based driver rewrote slot 2 (`0x4ed4`) to FORWARD at every boot; airjinkela's `rma_stp` experiment (same SDK lineage) wrote `0x4f34` = 2 and reported traps reaching the CPU port. Slots get trap, VLAN-leaky, CKEEP 0, isolation-leaky 0 (fail-closed), storm bypass and trap priority as read. Whether `0x4f34` or `0x5350` steers RMA traps, the reset actions, and the RTL8_4 tag and REASON of trapped frames are unmeasured. The `0457` tag_rtl8_4 patch is right only while these traps hold (CF5). |
| VLAN table reset: `0x4e14` bit 3 TABLE_RST, then VIDs 2, 4094 and 4095 sampled, and every VID from 2 written 0 if one has a member, untag or FID/MSTI bit (23:0; SVLAN check and IVL alone are only logged) | RTL8373 SDK `dal_rtl8373_vlan_reset()`; the SDK-based driver reset the table at every setup | **SDK-only lineage, disposition open.** Hardware-used by the old driver without a poll; that the bit clears itself (10 ms poll, else written 0), resets the whole table, and that writing it 0 starts nothing, are unmeasured. Every later `0x4e14` write keeps it 0. A failure turns bridge offload off (TE1). |
| VLAN classification controls: `0x4e14` bit 0 VID0_TYPE (written 0) and bit 1 VID4095_TYPE (1); per-VID learning-disable slots `0x4e30`/`0x4e34` (VALID 0, VID 12:1, action 13; written 0); port-based FID enable `0x4e38` (available ports written 0); egress keep `0x6728 + (port / 3) * 4`, 10 bits per port (read only, expected 0) | RTL8373 SDK `rtl8373_reg_definition.h` VLAN control fields, `RSV_VID_ACTION_UNTAG`/`_TAG`, `vlan_init`; ZTE uses port-based FID on VLAN-unaware ports | **SDK-only lineage, disposition open.** Bit positions from the SDK; the VID-type meanings are inferred from the action names (VA3); that a port-based FID would override the IVL keying of the FDB code is unknown, hence off. Reset values never recorded (T6 prints them). |
| L2 actions: `0x5350` bit 8 lookup-hit isolation action (1 = handle as unknown) and bits 7:6 hash-full action (0 = forward without learning); learn-limit action `0x4f80` 1:0 (0 = forward without learning); source-port permit `0x4f84` (user-port bits 0) | RTL8373 SDK `dal_rtl8373_l2_lookupHitIsolationAction_set`, `_hashFull_set`, `_limitLearningCntAction_set` (one global field), `dal_rtl8373_srcPortPermit_set`; the SDK-based driver cleared `0x4f84` at setup | **SDK-only lineage, disposition open.** Encodings from the SDK. A bridged port's mask lists itself, so a hit on the ingress port is inside isolation whatever bit 8 would do with it, and the source-port filter must drop it (B9). Unknown: whether the learn-limit action applies at limit 0 (L5). |
| Left at reset, never written: SA-moving forbid `0x535c` (logged at setup); IP multicast VLAN/isolation leaky `0x5088`/`0x508c`; RMA trap priority and storm bypass; per-port PTP actions `0x4f20`/`0x4f24`; RMA 10–2f (`0x4eec`–`0x4f0c`), CDP `0x4f10` and SSTP `0x4f14` | RTL8373 SDK only | Recorded so that the bench can point at them: a station move that waits for ageing (CF5), IP multicast crossing isolation or VLANs (B3), and a non-link-local DA that the switch traps, which the `0457` tagger patch still marks as forwarded, so the offloaded bridge does not relay it (CF7; reset values unrecorded in the SDK). |

## P1 research boundary

[P1-RESEARCH.md](P1-RESEARCH.md) maps potential VLAN/L2/CIST/BPDU follow-up
work to the pinned sources above and the Linux v6.18 DSA contract. Those rows
are research only: they do not extend the implemented feature-to-source map
or certify new definitions for import. The table engine follows the IVL
reading of VLAN bit 25 and L2 bit 29 (SDK, Air, ZTE) and never treats either
bit as validity; the conflicting RTLPlayground prose stays an open bench
question. Every newly retained field/operation needs a ledger entry and
source disposition; hardware validation remains a separate requirement.

## Excluded material and candidate licensing

The module object list contains `rtl8372n.c`, `rtl837x_common.c`,
`rtl837x_mdio.c`, `rtl837x_table.c`, `rtl8372n_l2.c` and `rtl8372n_vlan.c`. It excludes `rtk-api`, restricted generated
headers, all PHY/SerDes patch arrays, GPIO, swconfig and debug SDK interfaces.
The small register header is confined to used definitions and the ledger above.

The adapted driver files declare GPL-2.0-or-later; `rtl837x_table.c`,
`rtl8372n_l2.c` and `rtl8372n_vlan.c` declare GPL-2.0-only (see above). The combined module's `MODULE_LICENSE("GPL")` is kernel metadata,
not a claim that it resolves the origin of all register facts. The MIT input
notice is retained in [NOTICE](NOTICE). The existing package `LICENSE` file is
unchanged. The target kernel patch `0457-net-dsa-tag_rtl8_4-...` changes
`net/dsa/tag_rtl8_4.c` (GPL-2.0) outside the module; its header still needs
the author's Signed-off-by.

## Outstanding disposition

- Review retained Air/ZTE lineage with the source owners and maintainers,
  including [Airjinkela's SDK disclosure](https://github.com/RuijieNetworksCommunity/rtl837x-gsw-driver/issues/2#issuecomment-5946313878)
  and [JiaY-shi's source question](https://github.com/JiaY-shi/rtl83xx/issues/1).
- Confirm or independently source the SDS field/polarity facts still flagged
  above, with special attention to TX polarity semantics and Air-derived
  switch-specific code. Do not call the candidate fully provenance-clean
  until the retained lineage has a defensible disposition.
- Build for the real OpenWrt target and validate every row on BE9300.
- Decide the SDK-only rows that bridge offload added (VLAN table reset and
  classification controls, L2 actions including new-SA and hash-full,
  TAG_ONLY, egress transparency, `0x5cb0` bit 13, the CPU tag-aware mask
  layout) and the MAC status row, which P0 already used.
  MNeroba/openwrt-flint3#1 and #2 are open pull requests; keep their
  attribution in any submission.
- No source-owner Signed-off-by is inferred from a repository license or from
  our attribution. Obtain any required records before upstream submission.

## Corrections to commit messages

The branch's commits are not rewritten; where their messages and this ledger
disagree, the ledger, the code and FIRST-HARDWARE-TEST.md are the reference.

- `c14f9e1c12` (link-local traps) credits Maksims Neroba's "#2 bench results
  (BPDUs reaching the CPU in every CIST state) and his RMA/PTP
  trap-destination analysis". Release 12 of MNeroba/openwrt-flint3#2
  captured a peer's BPDUs at the CPU-facing standalone lan4 in ASIC CIST
  states forwarding, blocking, listening and learning (DISABLED was not
  run), with its static entry for 01:80:c2:00:00:00 and P0's CPU-only
  isolation in place. Which of these delivered them, and their REASON, are
  unresolved, so the result does not qualify this driver's RMA trap (CF2).
  The trap-destination analysis is from airjinkela/rtl837x-dsa-driver#2
  (also his). Its SDK-only list includes "the port 9 registers": the
  Isolation row now says so too (the stride is hardware-proven for ports
  0–8, port 9 itself is SDK-only).
- `4628c2fea2` (STP, fast-age, FDB) says that mode 0 sparing static entries
  is "documented by the SDK and RTLPlayground only": MNeroba#2 also observed
  a static multicast entry survive a flush of another port (Flush row).
- `cb96127fef` (VLAN-unaware bridge), B4: with br-lan offloaded, DSA
  refuses a second bridge first ("Range of offloadable bridges exceeded"),
  before the driver's VLAN-aware refusal of that commit (bisection only).
- `9cdb8ef409` (VLAN-aware bridges) lists "the bridge PVID applied only while
  filtering (`rtl837x_commit_pvid_for_mode()`)" as hardware-proven: it is an
  old-driver rule, deployed but not benched separately (row of the VLAN-aware
  port mode). Its VA4 fallback ("restore the join refusal") is now software
  mode or a VLAN-unaware br-lan.
- `d74b109c2e` (tagger patch): its CF5 "zero copies with STP on" applies to
  BPDUs only, as `3c4eccb874` says.
- `4d7bfce72d` (8021q uppers), U2: lan1 is port 7, so the line that must
  not appear is `port 7 joined bridge`, not `port 4`.
- `00978ab84f` (setup gate): netifd enslaves through `SIOCBRADDIF`, which
  carries no extack; only `ip link set ... master` shows `hardware bridging
  disabled: ... at setup`.
- `39439676ab` (VID 0/1 entry), bench step 2: after the no-VID add on lan1,
  `bridge fdb add ... dev lan2 master static vlan 1` fails with EEXIST; B10
  uses `bridge fdb replace`, and since `a6a9de8fec` VID 1 is inert in a
  VLAN-unaware br-lan, so only `static entry for port 7` appears.
- `885ba3be4d` (assisted learning): an address that does not fit logs
  DSA's `failed to add ... -28` once per offloaded LAN port, and `failed to
  delete ... -2` per port when the bridge later ages or moves it (AL1).
- `b2f3c8a724` (same VLAN mode), "VA6 under load": tagged VID 20 frames at
  lan2 are expected while the whole bridge is at vlan_filtering 0; "never"
  holds only at 1 and during mixed-mode toggles (VA6).
- Bench checklists that say "with dynamic debug" or "build with -DDEBUG"
  (such as `1520a11661`'s) mean the `RTL837X_DSA_DEBUG` image of
  `77629ce3ce`: the target kernel has no dynamic debug.
- Later commits supersede design statements of earlier ones: fatal VID 0,
  VLAN 1 mismatch and L2-clear failures (`518853abc1`, `9cdb8ef409`;
  `4a90d52ff4`), assisted learning off (`cb96127fef`; `885ba3be4d`), no FDB
  alias record (`4628c2fea2`; `39439676ab`), `0x4f84` and `0x5350` bit 8 at
  reset (`cb96127fef`; `36f8791bad`), no tagger change (`cb96127fef`;
  `d74b109c2e`), a failed VLAN delete dropping the membership and no sampling
  of never-written VIDs (`9cdb8ef409`; `8c08fe15c2`, `6e3e42f093`), and the
  setup readback count (79 lines with five available ports).
