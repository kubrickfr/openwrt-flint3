# PPE hardware offload: all TCP flows silently declined (found 2026-09-24)

First hardware test of the PPE flow-offload path on the bench Flint 3
(router build `20260924-d5f2217629`, openwrt `r35700+50-b12c85403f`).

## Setup

- Bench GL-BE9300 flashed with the router flavour (`sysupgrade -n`, clean config).
- WAN 10.20.30.63/24 (2.5G), LAN 192.168.1.1/24.
- `firewall.@defaults[0].flow_offloading=1`, `flow_offloading_hw=1`.
- Flowtable `ft` comes up with `flags offload`, devices `{lan1..lan4, wan}`;
  `hw-tc-offload: on` on all five.
- Traffic: `iperf3` from a LAN client (192.168.1.10) to 10.20.30.10 through
  the router's NAT — a routed + SNAT'd TCP flow, ~950 Mbit/s (1G LAN port).

## Symptom

Flows reach `[OFFLOAD]` (software flowtable) but never `[HW_OFFLOAD]`:

    tcp src=192.168.1.10 dst=10.20.30.10 sport=58804 dport=5201 ... [OFFLOAD]

`/proc/net/nf_conntrack` prints `[HW_OFFLOAD]` in preference to `[OFFLOAD]`
(`nf_conntrack_standalone.c:367`), so the hardware bit is genuinely unset.
The only driver log is an occasional `ppe_offload: replace failed at exists: -17`
(that is the second of the two block callbacks we register — see below).

## Root cause

`nf_flow_rule_match()` (`net/netfilter/nf_flow_table_offload.c`) builds **every**
TCP flow with a non-zero TCP flag mask:

    case IPPROTO_TCP:
        key->tcp.flags = 0;
        mask->tcp.flags = cpu_to_be16(be32_to_cpu(TCP_FLAG_RST | TCP_FLAG_FIN) >> 16);

`ppe_offload_flow_replace()` declines whenever that mask is set:

    if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_TCP)) {
        flow_rule_match_tcp(rule, &tcp);
        if (tcp.mask->flags)
            return ppe_offload_decline("tcp flag mask %#x not honoured", ...);
    }

So **100% of TCP flows are refused**; only UDP/UDPLITE could ever offload. The
decline is invisible because `ppe_offload_decline()` is `pr_debug` and this
kernel has `CONFIG_DYNAMIC_DEBUG` unset.

## A first hypothesis that turned out to be WRONG

`struct ppe_flow_ipv4_nat_entry` has a `deacclr_en` field, annotated in our own
header as "de-accelerate ... under hardware-defined conditions (e.g. a TCP
control flag)". It is encoded (`PPE_FLOW_TBL_DE_ACCE`, `ppe_flow_ipv4.c:55`) but
never assigned, so it is always false.

My first reading was that setting it for TCP flows would honour the netfilter
contract. **That is wrong** — the annotation is our own guess, not vendor
documentation, and checking the SDK disproves it. See
*DE_ACCE semantics* below: `DE_ACCE` redirects every matched packet to the CPU.

For comparison, `mtk_ppe_offload.c` and `airoha_ppe.c` do not inspect
`tcp.mask->flags` at all — they accelerate TCP unconditionally and rely on
flowtable ageing.

## Secondary observation: duplicate block callbacks

`ppe_offload_setup_tc_block()` allocates the flow block cb with `cb_ident = netdev`,
so the conduit (`lan`, shared by lan1..lan4) and `wan` plausibly each add a callback
to the same flowtable block with the same `cb_priv`. That would make every
`FLOW_CLS_REPLACE` reach the driver twice with one cookie: the first inserts, the
second returns `-EEXIST`. Netfilter tolerates this (`if (err < 0) continue;` and
`ok_count` counts successes), so it would be cosmetic log noise.

This remains a **hypothesis** -- it has not been confirmed by instrumenting the
callback registration. What the hardware test does confirm is that the accounting
is *not* doubled (see below), which is consistent with a second STATS call
returning a zero delta, but does not by itself prove the two-callback model.

## UDP path: hardware offload and FLOW_CLS_STATS both validated on silicon

Because only TCP is declined, UDP could be used to exercise the whole path with
this same image. `iperf3 -u` from the LAN client through the router's NAT:

    udp src=192.168.1.10 dst=10.20.30.10 sport=50005 dport=5201 ... [HW_OFFLOAD]

**Forwarding really is in hardware.** Over a 15.14 s steady-state window at
~310 Mbit/s:

| metric | delta |
|---|---|
| `/proc/net/softnet_stat` processed (all CPUs) | **530 packets** |
| CPU idle (`/proc/stat`) | 5986 of 6056 ticks = **98.8 % idle** |
| `lan` netdev rx (`/proc/net/dev`) | 392,385 packets / 588 MB |
| PPE `cpu_code` 162 | 22 packets in 12 s |

The CPU processed essentially nothing while ~390 k packets crossed the box, so
the `lan`/`wan` numbers in `/proc/net/dev` are PPE **port MIB** counters that
include hardware-forwarded frames. They are *not* host deliveries and must not
be read as evidence of CPU involvement.

**`FLOW_CLS_STATS` accounting is exact.** A bounded 30 s / 200 Mbit/s run,
sampling `/proc/net/nf_conntrack` every 2 s, gave a clean staircase rising in
uniform steps of 53,867 packets every ~4 s (so the flowtable polls stats about
every 4 s). At teardown the hardware entry went away and the tail of the run
fell back to software, creating a fresh conntrack entry:

    hardware-counted   488,928 packets   730,458,432 bytes
    software tail       29,044 packets    43,391,736 bytes
    total              517,972 packets

`iperf3` reported **517,972** datagrams sent. The totals match exactly, which
confirms:

- the counter read (`ppe_flow_counter_get`) and the masked delta
  (`ppe_offload_counter_delta`) report correctly against real silicon;
- there is **no double-counting** despite any repeated callback invocation;
- `ppe_flow_counter_clear()` on entry add gives a clean baseline.

For offloaded flows conntrack accounting is fed *exclusively* by
`flow_offload_work_stats()` -> our `ppe_offload_flow_stats()` ->
`flow_stats_update()`, so with the CPU idle these counts could not have come
from any software path.

**Byte counts include L2.** 773,850,168 bytes / 517,972 packets = 1494.0 bytes,
against 1476 bytes on the wire (1448 payload + 8 UDP + 20 IP). The extra 18
bytes per packet are the 14-byte Ethernet header plus the 4-byte DSA tag: the
PPE byte counter measures the full L2 frame, whereas the software path reports
L3 bytes. Packet counts are unaffected. This makes conntrack/nlbwmon byte
accounting for offloaded flows read ~1.2 % high at this MTU, and materially
higher for small packets. Worth deciding whether to subtract
`packets * (ETH_HLEN + tag)` before reporting.

**First hardware confirmation of the programming pipeline.** Reaching
`[HW_OFFLOAD]` means the public-IP, nexthop, host and flow-table operations all
succeeded on silicon, including the host table, whose APPE/MPPE register-map
fix (patch 0433) had until now only been compile-tested and cross-checked
against the encoder.

## Still not validated

Counter **wrap**. The 32-bit packet counter needs ~2^32 packets and the 40-bit
byte counter ~1 TB, roughly 2.5 h at 950 Mbit/s, so it cannot be reached
organically in a test session. The masked-subtraction logic stays unexercised.

## DE_ACCE semantics — resolved against the vendor SDK (do NOT use it)

Checked in `qca-ssdk-2025.05.30~446db12b` and `qca-nss-ppe-2024.09.25~306c5094`.

Our bit position is **correct**: vendor `IN_FLOW_TBL_DE_ACCE` is at entry bit
offset 60, and our `PPE_FLOW_TBL_DE_ACCE` is word[1] BIT(28) = bit 60. But the
*meaning* is not what our header comment guessed:

- `DE_ACCE` on a flow entry means "de-accelerate this flow", full stop. It is not
  conditioned on TCP flags.
- The action is a **global** setting, `L3_ROUTE_CTRL.FLOW_DE_ACCE_CMD`
  (offset 18, len 2), whose reset default is **0x3**.
- `fal_fwd_cmd_t` (`fal_type.h:123`) is
  `0 = FAL_MAC_FRWRD, 1 = FAL_MAC_DROP, 2 = FAL_MAC_CPY_TO_CPU,
  3 = FAL_MAC_RDT_TO_CPU`.

So `deacclr_en = true` means **redirect every matched packet to the CPU** —
`[HW_OFFLOAD]` with zero acceleration. Setting it to "honour the FIN/RST mask"
would have been exactly wrong. The `_DE_ACCE` siblings in `L3_ROUTE_CTRL`
(`IP_MTU_FAIL_DE_ACCE`, `FLOW_SRC_IF_CHECK_DE_ACCE`,
`FLOW_SYNC_MISMATCH_DE_ACCE`, ...) confirm the pattern: de-acceleration is the
consequence of an *exception*, never a TCP-flag matcher.

The header comment on `@deacclr_en` in `ppe_flow_ipv4.h` is our own annotation
and is misleading; it should be corrected to describe the global-CMD behaviour.

## Where TCP FIN/RST handling actually lives

The hardware has eight global TCP-flag exception matchers:

- `TPR_L4_EXCEPTION_PARSING_CTRL_0` @ `0x28`, `_1` @ `0x2c` (INC 0x4), each
  holding `TCP_FLAGSn` (6-bit value) + `TCP_FLAGSn_MASK` (6-bit mask).
- They raise `FAL_SEC_EXP_TCP_FLAGS_0..7` (exception ids 46-53), surfacing as
  CPU codes `PPE_DRV_CC_TCP_FLAGS_0..7` (47-54).
- Per-exception action lives in the `TPR_EXCEPTION_CTRL_*` block (`0x700`, `0x740`).

Programming one slot for FIN and one for RST would punt exactly those packets to
the CPU while the rest of the flow stays accelerated — honouring the netfilter
contract precisely. **Our driver currently implements none of these registers.**

## Decision — Option A is NOT safe; the fix is Option B

I initially favoured "just drop the decline" on the grounds that
`mtk_ppe_offload.c` and `airoha_ppe.c` don't inspect `tcp.mask->flags`. **That
reasoning was wrong** — not inspecting the mask is not the same as ignoring
teardown. Both drivers handle FIN *in hardware*:

- MediaTek enables `MTK_PPE_TB_CFG_AGE_TCP_FIN` at init with
  `MTK_PPE_BIND_AGE1_DELTA_TCP_FIN = 1` (vs `AGE_TCP` = 60), and defines CPU
  reasons `MTK_PPE_CPU_REASON_TCP_FIN_SYN_RST` (0x0c) and
  `MTK_PPE_CPU_REASON_HIT_BIND_TCP_FIN` (0x10).
- Airoha sets `PPE_TB_CFG_AGE_TCP_FIN_MASK` with
  `PPE_BIND_AGE1_DELTA_TCP_FIN = 1` against `DELTA_TCP = 60`.

And stock QCA on this very silicon does the same thing, via the exception path:
`ppe_drv_exception_tcpflag_list[] = { FIN, SYN, RST }`, each programmed with
`tcp_flags[i] == tcp_flags_mask[i]`, and each registered in
`ppe_drv_exception_list[]` as `FAL_MAC_RDT_TO_CPU` +
`PPE_DRV_EXCEPTION_DEACCEL_EN` for `L2_FLOW_HIT | L3_FLOW_HIT`.

### Measured cost of doing nothing

`NF_FLOW_CLOSING` is set in exactly one place, `nf_flow_table_ip.c:38`, on the
**software** forwarding path. A hardware-offloaded flow never executes it, so
the bit is never set, and `flow_offload_fixup_ct()` takes the `else` branch:

    closing = false  ->  timeout = tn->timeouts[tcp_state]   /* ESTABLISHED */

On the bench box:

    nf_conntrack_tcp_timeout_established = 7440
    nf_conntrack_tcp_timeout_close       = 10
    nf_flowtable_tcp_timeout             = 30
    nf_conntrack_max                     = 55808

So every hardware-offloaded TCP connection would linger as ESTABLISHED for
**7440 s instead of 10 s** after it closes. Sustained ~7.5 offloaded TCP
connections/second fills the 55,808-entry table — well within reach of ordinary
browsing. Option A trades a silent TCP decline for conntrack exhaustion under
churn. Not acceptable.

### Option B register map (IPQ5332 = APPE/MPPE variant)

TCP flag value/mask pairs, 8 slots across 4 registers:

    IPR_CSR_BASE_ADDR + L4_EXCEPTION_PARSING_CTRL_{0,1,2,3}_REG_ADDRESS
                        (0x28, 0x2c, 0x30, 0x34)

Per-exception action and flow-type enable:

    L3_EXCEPTION_CMD          IPE_L3 + 0x81c   (action + de-accelerate)
    L3_FLOW_HIT_EXP_CTRL      IPE_L3 + 0x1678  (enable, on L3 flow HIT)

**Second trap, found while implementing:** the obvious-looking
`L3_EXP_L3_FLOW_CTRL` (IPE_L3 + 0xcdc) is the *HPPE* path. On APPE the SDK
splits it into hit and miss registers and sets the generic one to **0** whenever
it uses them — see the `#if defined(APPE)` branch of
`adpt_hppe_sec_l3_excep_ctrl_set()`. Writing the generic register on this
silicon arms nothing. The right one is `L3_FLOW_HIT_EXP_CTRL`.

Scoping the exception to a flow *hit* is also what keeps the change contained:
a FIN on a connection nobody offloaded never raises the exception, so
non-offloaded traffic is untouched.

**Trap:** each of these is defined twice in the vendor headers under different
`#if` branches, and the IPQ5332 is APPE/MPPE, not HPPE:

    IPR_CSR_BASE_ADDR        0x1e0000 (HPPE)  vs  0x002000 (APPE/MPPE)
    L3_EXCEPTION_CMD         0x81c    (HPPE)  vs  0x544    (APPE/MPPE)
    L3_EXP_L3_FLOW_CTRL      0xcdc    (HPPE)  vs  0x9c4    (APPE/MPPE)

This is exactly the mistake patch 0433 already cost us once. Whichever branch is
taken must be verified the same way 0433 was, by cross-checking against a live
vendor register dump, not by reading the header top-down. See
[[project-flint3-nsscc-wrong-register-map]].

### Work items for the rebuild

1. Add the L4 exception parsing registers + the L3 exception command/flow-ctrl
   registers (APPE/MPPE offsets, verified against hardware).
2. At init, program FIN / SYN / RST with value == mask, action redirect-to-CPU,
   enabled on L3 flow hit.
3. Remove the blanket TCP decline in `ppe_offload_flow_replace()`.
4. Correct the misleading `@deacclr_en` comment in `ppe_flow_ipv4.h`.
5. Make declines observable (ratelimited print or per-reason debugfs counters) —
   `pr_debug` with `CONFIG_DYNAMIC_DEBUG` off is why this cost a full cycle.
6. `luci-app-lldpd` in the ap/router flavours (already in the chain script).

### Hardware test plan

- A TCP flow reaches `[HW_OFFLOAD]` with `softnet_stat` flat and CPU idle.
- Close a connection with FIN: conntrack goes to CLOSE and disappears in ~10 s,
  not ~2 h. Repeat with RST.
- Churn ~500 short connections and watch `nf_conntrack_count` settle rather than
  climb. This is the test that actually validates the fix.

## Implementation notes (2026-09-24, patch 0442)

Two bugs the implementation hit, both worth remembering.

**1. The enable register is not the obvious one.** `L3_EXP_L3_FLOW_CTRL`
(IPE_L3 + 0xcdc) is the HPPE path. On APPE the SDK splits it into flow-hit and
flow-miss registers and explicitly clears the generic one whenever it uses them
(`adpt_hppe_sec_l3_excep_ctrl_set()`, `#if defined(APPE)` branch). The register
that actually arms the exception is `L3_FLOW_HIT_EXP_CTRL` (IPE_L3 + 0x1678).

**2. `ppe_offload_init()` runs before the PPE is clocked.** In `ppe.c` the probe
order is:

    ppe_offload_init()          <-- allocations + indirect block registration
    ppe_clock_init_and_reset()
    ppe_hw_config()
    edma_setup()

so any register write from `ppe_offload_init()` lands on a block that is still
unclocked and in reset. The writes do not stick and reads return reset-state
junk -- the first hardware boot read back `0x3` instead of the `0x0101`
written. Hardware programming therefore has to happen in a separate
`ppe_offload_hw_init()` called *after* `ppe_hw_config()`.

**The readback guard paid for itself immediately.** It turned what would have
been a silent no-op -- TCP flows accepted while the exceptions were not actually
armed, i.e. exactly the conntrack-exhaustion scenario the fix exists to prevent
-- into one explicit line on the first boot:

    qcom_ppe 3a000000.ethernet: TCP exception parser readback mismatch (0x3);
    not offloading TCP

Keep that pattern for any future PPE register work: write, read back, and refuse
the feature rather than trusting the map.

Promoting the decline diagnostic out of `pr_debug` also worked on the first try
-- `ppe_offload: declined - odev=lan4 not an edma netdev` is now visible on a
stock build, where before it was invisible.

## Hardware validation of the fix (bench BE9300, 1 Gbit LAN client)

**Exceptions program and verify.** No readback mismatch after the ordering fix.

**TCP offloads.** `iperf3` LAN -> WAN through NAT:

    tcp src=192.168.1.10 dst=10.20.30.10 sport=57502 dport=5201 ... [HW_OFFLOAD]

    1.64 GBytes in 15.00 s = 937 Mbit/s   (line rate on a 1G port)

Over a 15.13 s steady-state window: PPE MIB counted 1,223,626 packets while
`softnet_stat` processed 88,726 and the CPU stayed **96.0% idle** (5812 of 6052
ticks). The residual CPU share is the reply direction — ACKs, whose egress is a
DSA user port and so is still declined (`odev=lan4 not an edma netdev`).

**Teardown is correct — the whole point of the change.** After the offloaded
connection closed with FIN:

    t=300s  tcp 6  9 CLOSE
    t=303s  tcp 6  6 CLOSE
    t=306s  tcp 6  3 CLOSE
    t=309s  tcp 6  0 CLOSE
    t=312s  (gone)

CLOSE state on the 10 s timeout, not ESTABLISHED on 7440 s.

**No accumulation under churn.** 40 back-to-back `iperf3 -t 1` runs (80 TCP
connections including control):

    baseline                 conntrack_count = 96
    immediately after         count = 100
    after 45 s settling       count = 20
    lingering dport=5201      0

Without the fix each of those 80 would have held an ESTABLISHED entry for over
two hours.

## Remaining gaps after 0442

- The reply/inbound direction is still declined (DSA user port egress), so ACKs
  traverse the CPU. That is the next acceleration step, not a regression.
- Conntrack's original-direction packet/byte counters read 0 for an offloaded
  **TCP** flow, while UDP accounts exactly (see above). Forwarding is unaffected
  and the CPU-idle measurement proves hardware is doing the work, but the
  accounting discrepancy is unexplained and worth a look.
- Counter wrap is still unexercised.

## Review fixes and second hardware round (2026-09-24, later)

Both reviewers passed 0442 with 0 Critical/High. Their Mediums are now fixed:

- **Readback was only checking one slot.** It verified the FIN parser field and
  FIN's hit-enable, and nothing else -- not SYN, not RST, and crucially not the
  `L3_EXCEPTION_CMD` action word. A slot that raised its exception while the
  action sat at its reset value (FORWARD) would accelerate the teardown anyway
  and still report success. It now loops all three slots x four registers
  (parser value+mask, action, hit-enable, miss-enable-clear). **All twelve check
  out on silicon.**
- **Decline logging would have spammed.** `pr_info_ratelimited` fires for the
  normal case here -- the reply direction of every NAT connection, every IPv6
  flow -- and `flow_offload_refresh()` retries ~1/s per live flow, so it would
  sit at the ratelimit ceiling permanently. Now `pr_info_once` per call site:
  each reason announces once per boot then goes quiet. Confirmed on hardware --
  one line, deduped, where the old build produced repeats.
- **Gate was keyed on the TCP dissector key**, so a hand-written tc filter
  lacking that key could reach hardware with exceptions unprogrammed. Now keyed
  on `ip_proto == TCP`.
- Flow-MISS enable explicitly cleared; exception-index `BUILD_BUG_ON` added;
  shared-ingress-L3-IF limitation documented in the file-level comment.

### Hardware offload proven directly

A router-side `tcpdump` during an offloaded flow saw **only the 3-way
handshake** -- 2x `[S]`, 2x `[S.]`, 2x `[.]` -- and nothing afterwards, while
~1 Gbit/s crossed the box. Packets that reach the CPU are visible there; these
did not. That is a cleaner proof than the earlier CPU-idle measurement.

### The RST case is still NOT isolated -- stated plainly

Aborting two hardware-offloaded connections with SIGKILL did give the right
outcome: conntrack reached **CLOSE with an ~8s timeout** and the entries were
gone, not ESTABLISHED/7440s.

But that does **not** prove the LAN-side RST exception did it. Captures show the
FIN/RST traffic on the **WAN** side, and none on the LAN side. That matches the
security reviewer's analysis exactly: because the reply direction is never
offloaded, the remote peer's FIN/RST always traverses software and sets
`NF_FLOW_CLOSING` on its own. So today the exceptions are a *second* line of
defence and are very hard to isolate by black-box test.

Consequence to carry forward: **the day reply-direction offload lands, these
exceptions become the only teardown mechanism** and this verification gap stops
being academic. A purpose-built test (a LAN client aborting with SO_LINGER{1,0}
while the far end stays silent) should be part of that milestone. A
cross-compiled helper for exactly this is at
`~/.claude/jobs/.../rstabort.c` -- it works, but its flows would not
hardware-offload on this bench for reasons unrelated to the patch (iperf3 flows
on the same box and build offload reliably; the EEXIST retry count was flat, so
that is not the cause either -- unexplained).

### Build trap that cost a cycle

Editing sources under `build_dir/` and running `make target/linux/compile` is
**not** safe if the patch file in `target/linux/qualcommbe/patches-6.18/` is
newer than the prepared stamp: the build re-runs *prepare*, re-applies the patch
series and silently discards the build_dir edits. The build then succeeds and
flashes an image without the changes. It cost a full flash-and-test cycle before
the missing string in the running module gave it away.

Rule: **the patch file is the source of truth.** Regenerate it *before*
building, and verify after the build that the source still contains the change
(`grep` for a distinctive new string in both the `.c` and the built `.ko`).

## Also seen

WAN link flapped several times during the session
(`qcom_ppe ... wan: Link is Down` / `Up - 2.5Gbps/Full`), unrelated to offload.
Worth a separate look.
