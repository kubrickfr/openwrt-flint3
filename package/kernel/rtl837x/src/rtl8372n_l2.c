// SPDX-License-Identifier: GPL-2.0-only
/*
 * RTL8372N L2 forwarding controls: CIST port state, lookup-miss and
 * source-address actions, ASIC IGMP snooping, and the trapping of link-local
 * control frames to the CPU.
 *
 * The CIST state helper is adapted from Maksims Neroba's rtl837x_stp.c in
 * MNeroba/openwrt-flint3#2, whose state encoding was bench-verified on a
 * GL-BE9300. The action, trap and IGMP fields follow the RTL8373 SDK
 * formerly shipped in this package; PROVENANCE.md records the source and
 * confidence of each.
 */
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/regmap.h>

#include "rtl837x.h"

/*
 * Link-local trap word: trap to the CPU, VLAN-leaky, not isolation-leaky,
 * CKEEP 0. The storm-control bypass (bit 3) is left as read.
 */
#define RTL8372N_RMA_TRAP_FIELDS	(RTL837X_RMA_ACTION | \
					 RTL837X_RMA_CKEEP | \
					 RTL837X_RMA_VLAN_LEAKY | \
					 RTL837X_RMA_PISO_LEAKY)
#define RTL8372N_RMA_TRAP_VALUE		(FIELD_PREP(RTL837X_RMA_ACTION, \
						    RTL837X_RMA_ACTION_TRAP) | \
					 RTL837X_RMA_VLAN_LEAKY)

/* Per-ingress-port actions that must forward rather than drop or trap. */
static const struct {
	u32 reg;
	const char *name;
} rtl8372n_port_act_regs[] = {
	{ RTL837X_L2_UNKNOWN_UC_ACT, "unknown unicast action" },
	{ RTL837X_L2_UNKNOWN_MC_ACT, "unknown L2 multicast action" },
	{ RTL837X_IPV4_UNKNOWN_MC_ACT, "unknown IPv4 multicast action" },
	{ RTL837X_IPV6_UNKNOWN_MC_ACT, "unknown IPv6 multicast action" },
	{ RTL837X_L2_UNMATCHED_SA_ACT, "unmatched source address action" },
};

int rtl8372n_stp_set_state(struct rtl837x_priv *priv, int port,
			   enum rtl8372n_stp_state state)
{
	/* Port 9 (8051) is never written. */
	if (port < 0 || port >= RTL8372N_NUM_PORTS ||
	    state > RTL8372N_STP_FORWARDING)
		return -EINVAL;

	return rtl837x_reg_bits_write(priv, RTL837X_STP_STATE,
				      RTL837X_STP_STATE_MASK(port), state);
}

/*
 * Standalone ports must pass traffic to the CPU, so every available port
 * starts FORWARDING whatever the reset left; unused ports are DISABLED, as
 * DSA's unused-port path would set them.
 */
static enum rtl8372n_stp_state rtl8372n_setup_stp_state(struct dsa_switch *ds,
							int port)
{
	return dsa_is_unused_port(ds, port) ? RTL8372N_STP_DISABLED :
					      RTL8372N_STP_FORWARDING;
}

static void rtl8372n_port_act(struct dsa_switch *ds, u32 *mask, u32 *val)
{
	struct dsa_port *dp;

	*mask = 0;
	*val = 0;
	dsa_switch_for_each_available_port(dp, ds) {
		*mask |= RTL837X_L2_PORT_ACT_MASK(dp->index);
		*val |= RTL837X_L2_ACT_FORWARD <<
			__ffs(RTL837X_L2_PORT_ACT_MASK(dp->index));
	}
}

/*
 * Forwarding controls shared by standalone and bridged ports. On every
 * available port a lookup miss forwards to the class flood mask, so the
 * flood masks (and later the bridge flood flags) govern unknown traffic, and
 * a frame whose source address is known on another port is forwarded, so a
 * station that moves is never dropped. ASIC IGMP/MLD snooping stays off: the
 * bridge owns multicast and no MDB is offloaded.
 */
int rtl8372n_l2_setup(struct dsa_switch *ds)
{
	struct rtl837x_priv *priv = ds->priv;
	u32 act_mask, act;
	int port, i, ret;

	rtl8372n_port_act(ds, &act_mask, &act);
	for (i = 0; i < ARRAY_SIZE(rtl8372n_port_act_regs); i++) {
		ret = regmap_update_bits(priv->map, rtl8372n_port_act_regs[i].reg,
					 act_mask, act);
		if (ret)
			return ret;
	}

	ret = rtl837x_reg_bits_write(priv, RTL837X_IGMP_CTRL,
				     RTL837X_IGMP_CTRL_SNOOPING, 0);
	if (ret)
		return ret;

	for (port = 0; port < RTL8372N_NUM_PORTS; port++) {
		ret = rtl8372n_stp_set_state(priv, port,
					     rtl8372n_setup_stp_state(ds, port));
		if (ret)
			return ret;
	}
	return 0;
}

/*
 * Trap 01:80:c2:00:00:00 and 02-0f (STP, LACP, EAPOL, LLDP, ...) to the
 * external CPU so that the switch never forwards them between ports, which
 * must hold before any isolation is opened. Slot 1 (PAUSE) is left alone.
 * The destinations come first, so that no trap ever targets the 8051; the
 * CPU port and its tag are already set. VLAN-leaky lets a VLAN-aware port
 * without CPU membership still trap; isolation-leaky stays off because user
 * ports always reach the CPU and it would carry a forwarded control frame
 * across isolation if the action did not take. LLDP is trapped through the
 * per-DA words: LLDP_EN is cleared, and its own word is set the same way.
 */
int rtl8372n_trap_setup(struct dsa_switch *ds)
{
	struct rtl837x_priv *priv = ds->priv;
	unsigned int slot;
	int ret;

	ret = rtl837x_reg_bits_write(priv, RTL837X_RMA_PTP_TRAP_CTRL,
				     RTL837X_RMA_PTP_TRAP_CPU,
				     RTL837X_TRAP_CPU_EXTERNAL);
	if (ret)
		return ret;
	ret = rtl837x_reg_bits_write(priv, RTL837X_L2_CTRL,
				     RTL837X_L2_CTRL_TRAP_CPU,
				     RTL837X_TRAP_CPU_EXTERNAL);
	if (ret)
		return ret;

	/* The trap fields are not contiguous: no rtl837x_reg_bits_write(). */
	for (slot = 0; slot < RTL837X_RMA_LINK_LOCAL_SLOTS; slot++) {
		if (slot == RTL837X_RMA_SLOT_PAUSE)
			continue;
		ret = regmap_update_bits(priv->map, RTL837X_RMA_CTRL(slot),
					 RTL8372N_RMA_TRAP_FIELDS,
					 RTL8372N_RMA_TRAP_VALUE);
		if (ret)
			return ret;
	}
	ret = regmap_update_bits(priv->map, RTL837X_RMA_CTRL_LLDP,
				 RTL8372N_RMA_TRAP_FIELDS,
				 RTL8372N_RMA_TRAP_VALUE);
	if (ret)
		return ret;
	return rtl837x_reg_bits_write(priv, RTL837X_RMA_CFG,
				      RTL837X_RMA_CFG_LLDP_EN, 0);
}

/* Setup readback lines for the controls above, in the P0 readback format. */
void rtl8372n_l2_report_readback(struct dsa_switch *ds)
{
	struct rtl837x_priv *priv = ds->priv;
	u32 act_mask, act, stp_mask = 0, stp = 0;
	unsigned int slot;
	int port, i;

	for (port = 0; port < RTL8372N_NUM_PORTS; port++) {
		stp_mask |= RTL837X_STP_STATE_MASK(port);
		stp |= rtl8372n_setup_stp_state(ds, port) <<
		       __ffs(RTL837X_STP_STATE_MASK(port));
	}
	rtl8372n_report_p0_reg(priv, "STP port states", RTL837X_STP_STATE,
			       stp_mask, stp);

	rtl8372n_port_act(ds, &act_mask, &act);
	for (i = 0; i < ARRAY_SIZE(rtl8372n_port_act_regs); i++)
		rtl8372n_report_p0_reg(priv, rtl8372n_port_act_regs[i].name,
				       rtl8372n_port_act_regs[i].reg,
				       act_mask, act);
	/* Whole-register lines: the other fields show their reset values. */
	rtl8372n_report_p0_reg(priv, "IGMP/MLD snooping", RTL837X_IGMP_CTRL,
			       RTL837X_IGMP_CTRL_SNOOPING, 0);

	rtl8372n_report_p0_reg(priv, "RMA/PTP trap CPU",
			       RTL837X_RMA_PTP_TRAP_CTRL,
			       RTL837X_RMA_PTP_TRAP_CPU,
			       FIELD_PREP(RTL837X_RMA_PTP_TRAP_CPU,
					  RTL837X_TRAP_CPU_EXTERNAL));
	rtl8372n_report_p0_reg(priv, "L2 trap CPU", RTL837X_L2_CTRL,
			       RTL837X_L2_CTRL_TRAP_CPU,
			       FIELD_PREP(RTL837X_L2_CTRL_TRAP_CPU,
					  RTL837X_TRAP_CPU_EXTERNAL));
	for (slot = 0; slot < RTL837X_RMA_LINK_LOCAL_SLOTS; slot++) {
		if (slot == RTL837X_RMA_SLOT_PAUSE)
			continue;
		rtl8372n_report_p0_reg(priv, "RMA link-local trap",
				       RTL837X_RMA_CTRL(slot),
				       RTL8372N_RMA_TRAP_FIELDS,
				       RTL8372N_RMA_TRAP_VALUE);
	}
	rtl8372n_report_p0_reg(priv, "RMA LLDP trap", RTL837X_RMA_CTRL_LLDP,
			       RTL8372N_RMA_TRAP_FIELDS,
			       RTL8372N_RMA_TRAP_VALUE);
	rtl8372n_report_p0_reg(priv, "RMA LLDP_EN", RTL837X_RMA_CFG,
			       RTL837X_RMA_CFG_LLDP_EN, 0);
}
