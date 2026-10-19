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

## Code reuse and copyright-risk review

- The P0 implementation does not include Airjinkela's restricted generated
  register header, `rtk-api`, or the PHY/SerDes patch payloads. The module's
  object list is limited to the files listed below.
- `rtl837x_table.c` adapts Maksims Neroba's table layer from
  MNeroba/openwrt-flint3#1 (`b6c2da89`, table mutex, idle wait, single
  command write, verified VLAN write) and his L2 key lookup and per-port
  flush (`rtl837x_l2_flush_port()`, BUSY-only completion, mode restored only
  when idle) from #2 (`847e9f79`), both GPL-2.0-only, and follows the shape
  of Linux `rtl8365mb_table_query()` (GPL-2.0). It therefore declares
  GPL-2.0-only.
- `rtl8372n_l2.c` adapts the CIST state helper and encoding from Maksims
  Neroba's `rtl837x_stp.c`, and the bridge-to-CIST state mapping of his STP
  callback, in MNeroba/openwrt-flint3#2 (`847e9f79`, GPL-2.0-only) and
  therefore also declares GPL-2.0-only.
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
| SDS indirect `0x03f8/0x03fc/0x0400`, command fields | [Air access helper](https://github.com/airjinkela/rtl837x-dsa-driver/blob/7f3b64c7f3a37db1866fd80ba815d89974d995fd/src/rtl837x_common.c#L408-L472) and [generated definitions](https://github.com/airjinkela/rtl837x-dsa-driver/blob/7f3b64c7f3a37db1866fd80ba815d89974d995fd/src/rtl8373_reg_definition.h#L2967-L2985); [ZTE helper](https://github.com/cnjn/linux-mainline-zte-zxslc-sr1010/blob/07f8687248578d4be6931c665ff5d08bb6cc3d9d/drivers/net/ethernet/zte/zx279133-rtl8372n.c#L1761-L1845); [RTLPlayground register map](https://github.com/logicog/RTLPlayground/blob/f0aea3dcac056e3274fd39e1c76a7117471c37da/rtl837x_regs.h#L64-L88) and SDS operations | The address and command layout agree technically across the references. Air's author disclosed SDK-derived lineage; the ZTE lineage is unreviewed; RTLPlayground declares MIT, but the reviewed material does not establish independent derivation. No vendor header or patch array is imported. Resolve source lineage separately from hardware behavior. |
| SDS mode `0x7b20`, mode `0x1a`, polarity pages 0/6 and bits | RTLPlayground [mode constants](https://github.com/logicog/RTLPlayground/blob/f0aea3dcac056e3274fd39e1c76a7117471c37da/rtl837x_regs.h#L64-L88), [SDS init](https://github.com/logicog/RTLPlayground/blob/f0aea3dcac056e3274fd39e1c76a7117471c37da/rtl837x_init.c#L21-L90), [OEM companion settings](https://github.com/logicog/RTLPlayground/blob/f0aea3dcac056e3274fd39e1c76a7117471c37da/machine_init.c#L60-L77); Air [interface-to-mode mapping](https://github.com/airjinkela/rtl837x-dsa-driver/blob/7f3b64c7f3a37db1866fd80ba815d89974d995fd/src/rtl837x_common.c#L207-L220); ZTE [CPU-port-8 setup](https://github.com/cnjn/linux-mainline-zte-zxslc-sr1010/blob/07f8687248578d4be6931c665ff5d08bb6cc3d9d/drivers/net/ethernet/zte/zx279133-rtl8372n.c#L1986-L2040) | `0x1a` is identified as 10GBASE-R/10GR by Air and RTLPlayground. ZTE's value `0x0d` is used with a different USXGMII CPU-port-8 setup and is not a conflicting Flint3 setting. RTLPlayground documents RX swap at page 0/reg 0 bit 9 and page 6/reg 2 bit 13. For TX, RTLPlayground says OEM firmware sets companion SDS0 polarity bits at page 0 bit 8 and page 6 bit 14 for RTL8221B; ZTE also changes these bits in a board-specific sequence. This corroborates behavior but is not an independent clean source. The candidate applies these settings to both SDS lanes from board properties; lane mapping and each flag's effect have not been independently verified. Release-7 link/traffic tests exercise the board configuration as a whole, not each polarity field independently. Generic reset/patch sequences remain excluded; post-reset and polarity-specific coverage are still open. |
| RTL8_4 CPU tag `0x603c/0x6720/0x6724` | RTLPlayground `rtl837x_regs.h` CPU tag symbols; Air `set_tag_rtl`; Linux `net/dsa/tag_rtl8_4.c` | Native tagger; no copied tagger or private 802.1Q protocol. Header format, forwarding reasons and port identity need traffic tests. |
| VLAN 1, table `0x5cac/0x5cb8`, read data `0x5ccc`, PVID `0x4e1c`, filter `0x4e14/0x4e18` | RTLPlayground [`vlan_setup` / `vlan_get`](https://github.com/logicog/RTLPlayground/blob/f0aea3dcac056e3274fd39e1c76a7117471c37da/rtl837x_port.c); ZTE VLAN map | Minimal bootstrap only. Per-port ingress filter `0x4e18` is now cleared for VLAN-unaware ports; global filter `0x4e14` stays on, as in Linux rtl8365mb. VLAN 1 is written through the table engine below and read back; the word (`0x0203e0f8` on BE9300) is unchanged from P0. General VLAN callbacks are absent. |
| Table engine: `0x5cac` address 28:16, type 10:8 (3 VLAN, 4 L2), write bit 1, execute/busy bit 0; data `0x5cb8 + 4i` / `0x5ccc + 4i`; `0x5cb0` method 17:14, ENTRY_CLR 18, hit 12, index 11:0 | RTLPlayground `rtl837x_regs.h` table section, `port_l2_learned`, `doc/l2.md`; ZTE `RTL8372N_L2_*` method/hit/clear/index fields; MNeroba#1/#2 | Hardware: VLAN write/read (P0, #2) and an L2 multicast write plus method-0 lookup (#2). The 13-bit address and 3-bit type widths, and the method-7 port filter `0x5cb0` 22:19, are **SDK-only lineage, disposition open**. Every L2 operation now writes method, port and ENTRY_CLR explicitly; non-zero method and port values, next-entry walks and ACT_STS = 0 have never run. |
| VLAN entry bits 23:20 FID/MSTI, 24 SVLAN check, 25 IVL | Air `ivl_en`, ZTE `VLAN_IVL` (bit 25); RTL8373 SDK field list for 23:20 and 24 | Bit 25 written as 1 in every entry, as P0 did; RTLPlayground's prose calls it "valid" and the bench test is open. Bits 20–24 are **SDK-only lineage, disposition open**; the driver only ever writes them 0 and checks they read back 0. |
| L2 unicast entry: MAC, VID/FID 27:16, IVL 29, L3 28, port 31:30 + word 2 1:0, age 4:2, auth 5, static 16 | ZTE `rtl8372n_l2_encode_key` / unicast program and delete; RTLPlayground L2 dump and static management entry; SDK `_rtl8373_fdbStUser2Smi` | Add writes age 6 and auth 1 (SDK, old driver, ZTE; RTLPlayground uses age 7, auth 0). Delete rewrites the key with no port, age or flags (SDK, ZTE). Used by the DSA FDB callbacks: static entries only, DSA VID 0 mapped to VLAN 1 (so VID 0 and 1 share an entry), CPU-port entries allowed, delete only of a static entry of the same port, dump by the method-7 walk on bridged ports only. No unicast entry has been written or walked on hardware. |
| L2 table clear `0x53d8` bit 0 (write 1, reads 1 while busy) | RTL8373 SDK `dal_rtl8373_l2_table_clear` / `_clearStatus_get`; the SDK-based driver wrote it at every setup without polling | **SDK-only lineage, disposition open.** Hardware-used by the old driver; the busy poll and its duration are untested. |
| VLAN admission `0x4e10` (2 bits/port, 0 = all), egress tag mode `0x6738` (2 bits/port) | RTLPlayground `rtl837x_regs.h` `RTL837x_REG_INGRESS`, `rtl837x_port.c` `RTL837X_VLAN_PORT_EGR_TAG`; ZTE `RTL8372N_VLAN_ACCEPT_FRAME`, `RTL8372N_PORT_TAG_MODE` | Addresses and 2-bit per-port layouts are public. The tag-mode encoding (ORIGINAL 0, REAL_KEEP 3) follows Linux rtl8365mb `enum rtl8365mb_vlan_egress_mode` by analogy; the RTL8373 SDK formerly shipped in this package agrees. Needs readback and tagged-traffic tests. |
| VLAN egress transparency `0x4eb8 + (port / 3) * 4`, 10-bit ingress-port mask per egress port | No pinned public source. Only the RTL8373 SDK formerly shipped in this package (`RTL8373_VLAN_PORT_EGR_TRANS_*`, `dal_rtl8373_vlan_portTransparent_set`); the mechanism matches Linux rtl8365mb `rtl8365mb_port_set_transparent` | **SDK-only lineage, disposition open.** Needs a maintainer decision, or an independent characterisation on the bench (tagged VID != 1 reaching the CPU with the field set vs. cleared). |
| Isolation `0x50c0 + port * 4` | RTLPlayground isolation register; ZTE isolation operations | CPU-only matrix; unused ports cleared, now including the internal 8051 port 9 (`0x50e4`, and its learning limit `0x53a8`). Hardware bridge join/leave code removed. |
| Learning limit `0x5384 + port * 4`, mask bits 12:0; 4160 = learning on | ZTE `RTL8372N_L2_LEARN_LIMIT_*` (including `_MAX` 4160), bridge-flags learning update; RTLPlayground limit address; RTL8373 SDK `dal_rtl8373_l2_limitLearningCnt_set` (maximum = LUT size 4160) | Zero limit at setup. `rtl8372n_port_set_learning()` writes 4160 or 0 (user ports only; the CPU port stays 0); not called until the bridge flags exist. The SDK-based driver wrote 4160 on bridged ports and learned in hardware; that 0 stops learning and the limit-exceeded behavior are unmeasured. |
| Flood `0x5360`–`0x5370`, mask bits 9:0 | ZTE `RTL8372N_*FLOOD`, `rtl8372n_flood_port_set`; the SDK-based driver's bridge-flag mapping | CPU-only targets at setup; link-local control frames are trapped by the RMA row below. `rtl8372n_port_set_flood()` maps BR_FLOOD to `0x5360`, BR_MCAST_FLOOD to `0x5364`/`0x5368`/`0x536c`, BR_BCAST_FLOOD to `0x5370`, keeps the CPU bit and rolls back on error; not called yet. |
| CIST state `0x5310`, two bits per port: 0 disabled, 1 blocking, 2 learning, 3 forwarding | RTLPlayground `0x5310` and STP states; MNeroba#2 `rtl837x_stp_set_state`; RTL8373 SDK `dal_rtl8373_asicMstpPortStatus_set` | Hardware: MNeroba#2 read back 0/1/1/2/3 on port 4 and an administrative down/up 0→3 on BE9300. Setup now writes FORWARDING on every available port and DISABLED on unused ports 0–2 and 8; port 9 is never written. The DSA STP callback then writes every DSA port (blocking and listening → 1, unknown → 1). That states 0–2 block data frames is unmeasured. |
| Per-port flush `0x53d4` (port mask 9:0, START 16, BUSY 17) and mode `0x53dc` (1:0 by port/VID/FID, 2 = also static) | RTLPlayground `port_l2_forget_port()` (`0x53dc` = 0 "port-based, dynamic entries", START bit 16 + port); ZTE `RTL8372N_L2_FLUSH_*`; MNeroba#2 `rtl837x_l2_flush_port()`; RTL8373 SDK `dal_rtl8373_l2_ucastAddr_flush` | Hardware: MNeroba#2 flushes on ports 4–7 completed polling BUSY only, read back the port mask (`0x80`/`0x40`/`0x20`/`0x10`) and mode bits 2:0 = 0. Used by fast-age with mode 0, restored only when idle. That mode 0 removes learned entries and spares static ones is documented (RTLPlayground, SDK), never measured. |
| Ageing `0x5354` bits 15:0 = seconds × 5, clamped to 14–800 s | ZTE `zx279133_rtl8372n_set_ageing_time` (same factor and clamp, "200 ms ticks"); RTL8373 SDK `dal_rtl8373_l2_ageout_timer_set`; the SDK-based driver | **SDK-only lineage, disposition open**: ZTE's code cites the SDK DAL for the factor. The clamp has no SDK basis (its comment says 45–458 s). The driver clamps instead of failing a join. Hardware-used at 300 s by the SDK-based driver; real ageing and its link to the entry age field are unmeasured. |
| Lookup-miss actions `0x5374` (unknown unicast), `0x4f74` (L2 multicast), `0x4f78`/`0x4f7c` (IPv4/IPv6 multicast); unmatched-SA action `0x4f70`; two bits per port, 0 = forward (flood mask) | RTL8373 SDK `dal_rtl8373_l2_unknUc_action_set`, `unknMc/unknV4Mc/unknV6Mc_action_set`, `portUnmatchedSaBehavior_set` | **SDK-only lineage, disposition open.** Written 0 on every available port at setup; reset values and effect never measured. SA-moving forbid `0x535c` (the SDK's polarity comment contradicts the register name) is left at reset. |
| ASIC IGMP/MLD snooping enable `0x5290` bit 0 | RTL8373 SDK `dal_rtl8373_igmp_state_set`; the SDK-based driver enabled it at probe | **SDK-only lineage, disposition open.** Written 0 at setup (no MDB offload); the reset value is unrecorded. |
| Link-local trap: RMA words `0x4ecc + 4 * slot` (slots 0, 2–7 = 01:80:c2:00:00:00, 02–0f; slot 1 PAUSE untouched) and LLDP `0x4f18`, fields action 5:4 (1 = trap), CKEEP 2, VLAN-leaky 1, isolation-leaky 0; RMA_CFG `0x4f1c` LLDP_EN bit 3; trap CPU selects `0x4f34` 17:16 and `0x5350` 16:15 (2 = external CPU) | RTLPlayground `RTL837X_RMA0_CONF` (`0x4ecc`) and `port_rldp_on()` (addresses only); RTL8373 SDK `dal_rtl8373_asicRma_set`, `asicRmaLldp_set`, `RTL8373_RMA_PTP_TRAP_CTRL`, `dal_rtl8373_l2_trapPort_set` | **SDK-only lineage, disposition open** for every field; the SDK-based driver rewrote slot 2 (`0x4ed4`) to FORWARD at every boot; airjinkela's `rma_stp` experiment (same SDK lineage) wrote `0x4f34` = 2 and reported traps reaching the CPU port. Slots get trap, VLAN-leaky, CKEEP 0, isolation-leaky 0 (fail-closed), storm bypass and trap priority as read. Whether `0x4f34` or `0x5350` steers RMA traps, the reset actions, and the RTL8_4 tag and REASON of trapped frames are unmeasured. |

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
`rtl837x_mdio.c`, `rtl837x_table.c` and `rtl8372n_l2.c`. It excludes `rtk-api`, restricted generated
headers, all PHY/SerDes patch arrays, GPIO, swconfig and debug SDK interfaces.
The small register header is confined to used definitions and the ledger above.

The adapted driver files declare GPL-2.0-or-later; `rtl837x_table.c` and
`rtl8372n_l2.c` declare GPL-2.0-only (see above). The combined module's `MODULE_LICENSE("GPL")` is kernel metadata,
not a claim that it resolves the origin of all register facts. The MIT input
notice is retained in [NOTICE](NOTICE). The existing package `LICENSE` file is
unchanged.

## Outstanding disposition

- Review retained Air/ZTE lineage with the source owners and maintainers,
  including [Airjinkela's SDK disclosure](https://github.com/RuijieNetworksCommunity/rtl837x-gsw-driver/issues/2#issuecomment-5946313878)
  and [JiaY-shi's source question](https://github.com/JiaY-shi/rtl83xx/issues/1).
- Confirm or independently source the SDS field/polarity facts still flagged
  above, with special attention to TX polarity semantics and Air-derived
  switch-specific code. Do not call the candidate fully provenance-clean
  until the retained lineage has a defensible disposition.
- Build for the real OpenWrt target and validate every row on BE9300.
- No source-owner Signed-off-by is inferred from a repository license or from
  our attribution. Obtain any required records before upstream submission.
