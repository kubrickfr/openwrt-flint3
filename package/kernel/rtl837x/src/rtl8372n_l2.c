// SPDX-License-Identifier: GPL-2.0-only
/*
 * RTL8372N L2 forwarding controls: CIST port state, learning, flooding,
 * ageing, fast-age and the FDB, lookup-miss and source-address actions, ASIC
 * IGMP snooping, and the trapping of link-local control frames to the CPU.
 *
 * The CIST state helper and the bridge-to-CIST state mapping are adapted from
 * Maksims Neroba's rtl837x_stp.c and rtl8372n.c in MNeroba/openwrt-flint3#2,
 * whose state encoding was bench-verified on a GL-BE9300; fast-age uses his
 * port flush, adapted in rtl837x_table.c. The action, trap, IGMP,
 * learning-limit, flood and ageing fields follow the RTL8373 SDK formerly
 * shipped in this package; PROVENANCE.md records the source and confidence
 * of each.
 */
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/if_bridge.h>
#include <linux/kernel.h>
#include <linux/math.h>
#include <linux/minmax.h>
#include <linux/regmap.h>
#include <linux/time64.h>

#include "rtl837x.h"

#define RTL8372N_FLOOD_FLAGS	(BR_FLOOD | BR_MCAST_FLOOD | BR_BCAST_FLOOD)

/*
 * The ageing window of the SDK-based and ZTE drivers. The field could hold
 * up to 13107 s, but how it maps to real ageing has never been measured.
 */
#define RTL8372N_AGEING_MIN_MS	14000
#define RTL8372N_AGEING_MAX_MS	800000

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

/* The flood mask of each traffic class, by the bridge flag that drives it. */
static const struct {
	unsigned long flag;
	u32 reg;
} rtl8372n_flood_regs[] = {
	{ BR_FLOOD, RTL837X_L2_UNKNOWN_UC_FLOOD },
	{ BR_MCAST_FLOOD, RTL837X_L2_UNKNOWN_MC_FLOOD },
	{ BR_MCAST_FLOOD, RTL837X_IPV4_UNKNOWN_MC_FLOOD },
	{ BR_MCAST_FLOOD, RTL837X_IPV6_UNKNOWN_MC_FLOOD },
	{ BR_BCAST_FLOOD, RTL837X_L2_BROADCAST_FLOOD },
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
 * A port's learn limit is its learning switch: the LUT size (4160) or 0.
 * Only user ports ever learn; the CPU port's addresses are static entries.
 */
int rtl8372n_port_set_learning(struct dsa_switch *ds, int port, bool enable)
{
	struct rtl837x_priv *priv = ds->priv;

	if (port < 0 || port >= ds->num_ports ||
	    (enable && !dsa_is_user_port(ds, port)))
		return -EINVAL;

	return rtl837x_reg_bits_write(priv, RTL837X_L2_LEARN_LIMIT_REG(port),
				      RTL837X_L2_LEARN_LIMIT_MASK,
				      enable ? RTL837X_L2_LEARN_LIMIT_MAX : 0);
}

/*
 * Add user @port to, or remove it from, the flood masks of the classes in
 * @flags: BR_FLOOD unknown unicast, BR_MCAST_FLOOD unknown L2, IPv4 and IPv6
 * multicast, BR_BCAST_FLOOD broadcast. Every write also sets the CPU port's
 * bit, which no call removes. On an error the port's bits are put back as
 * they were, so a failed call changes nothing.
 */
int rtl8372n_port_set_flood(struct dsa_switch *ds, int port,
			    unsigned long flags, bool enable)
{
	u32 old[ARRAY_SIZE(rtl8372n_flood_regs)] = {};
	struct rtl837x_priv *priv = ds->priv;
	unsigned long tried = 0;
	u32 bit, cpu_mask;
	unsigned int i;
	int ret = 0, err;

	if (port < 0 || port >= ds->num_ports || !dsa_is_user_port(ds, port) ||
	    (flags & ~RTL8372N_FLOOD_FLAGS))
		return -EINVAL;

	bit = BIT(port);
	cpu_mask = dsa_cpu_ports(ds);
	for (i = 0; i < ARRAY_SIZE(rtl8372n_flood_regs); i++) {
		if (!(flags & rtl8372n_flood_regs[i].flag))
			continue;
		ret = rtl837x_reg_read(priv, rtl8372n_flood_regs[i].reg,
				       &old[i]);
		if (ret)
			break;
		/* A write that reported an error may still have landed. */
		__set_bit(i, &tried);
		ret = regmap_update_bits(priv->map, rtl8372n_flood_regs[i].reg,
					 bit | cpu_mask,
					 (enable ? bit : 0) | cpu_mask);
		if (ret)
			break;
	}
	if (!ret)
		return 0;

	for_each_set_bit(i, &tried, ARRAY_SIZE(rtl8372n_flood_regs)) {
		err = regmap_update_bits(priv->map, rtl8372n_flood_regs[i].reg,
					 bit, old[i] & bit);
		if (err)
			dev_err(priv->dev,
				"port %d: flood mask %#x not restored: %d\n",
				port, rtl8372n_flood_regs[i].reg, err);
	}
	return ret;
}

/*
 * Besides bridged ports, DSA sets standalone ports FORWARDING on open and
 * DISABLED on stop, the CPU port FORWARDING after setup and DISABLED at
 * teardown, and unused ports DISABLED. The ASIC has no listening state, so
 * LISTENING blocks, as in MNeroba#2; an unknown state blocks too.
 */
void rtl8372n_port_stp_state_set(struct dsa_switch *ds, int port, u8 state)
{
	struct rtl837x_priv *priv = ds->priv;
	enum rtl8372n_stp_state hw;
	int ret;

	switch (state) {
	case BR_STATE_DISABLED:
		hw = RTL8372N_STP_DISABLED;
		break;
	case BR_STATE_LISTENING:
	case BR_STATE_BLOCKING:
		hw = RTL8372N_STP_BLOCKING;
		break;
	case BR_STATE_LEARNING:
		hw = RTL8372N_STP_LEARNING;
		break;
	case BR_STATE_FORWARDING:
		hw = RTL8372N_STP_FORWARDING;
		break;
	default:
		dev_warn_ratelimited(priv->dev,
				     "port %d: unknown STP state %u, blocking\n",
				     port, state);
		hw = RTL8372N_STP_BLOCKING;
		break;
	}

	ret = rtl8372n_stp_set_state(priv, port, hw);
	if (ret)
		dev_err_ratelimited(priv->dev,
				    "port %d: failed to set STP state %u: %d\n",
				    port, state, ret);
	else
		dev_dbg(priv->dev, "port %d: STP state %u, CIST %u\n",
			port, state, hw);
}

/* Only user ports learn; DSA also fast-ages the CPU port at teardown. */
void rtl8372n_port_fast_age(struct dsa_switch *ds, int port)
{
	if (!dsa_is_user_port(ds, port))
		return;

	/* The flush logs its own failure. */
	rtl837x_l2_flush_port(ds->priv, port);
}

/*
 * Clamp instead of setting ds->ageing_time_min/max: DSA's -ERANGE would fail
 * a bridge join outright, without a software fallback. An ageing time of 0
 * (expire at once) becomes the minimum.
 */
int rtl8372n_set_ageing_time(struct dsa_switch *ds, unsigned int msecs)
{
	struct rtl837x_priv *priv = ds->priv;
	unsigned int secs;

	msecs = clamp_t(unsigned int, msecs, RTL8372N_AGEING_MIN_MS,
			RTL8372N_AGEING_MAX_MS);
	secs = DIV_ROUND_UP(msecs, MSEC_PER_SEC);
	dev_dbg(priv->dev, "L2 ageing time %u s\n", secs);
	return rtl837x_reg_bits_write(priv, RTL837X_L2_AGE_CTRL,
				      RTL837X_L2_AGE_UNIT,
				      secs * RTL837X_L2_AGE_UNITS_PER_SEC);
}

/*
 * DSA's FDB entries become static unicast entries keyed {MAC, VID} (IVL) on
 * a user port or on the CPU port, which holds the bridge's own addresses.
 * DSA uses VID 0 for a VLAN-unaware bridge, whose frames the switch puts in
 * VLAN 1, so VID 0 and 1 share one entry: the last add sets its port, and
 * deleting either removes it while it still points at that port.
 */
static int rtl8372n_fdb_vid(struct dsa_switch *ds, int port,
			    const unsigned char *addr, u16 vid,
			    struct dsa_db db, u16 *hw_vid)
{
	if ((!dsa_is_user_port(ds, port) && !dsa_is_cpu_port(ds, port)) ||
	    (db.type != DSA_DB_PORT && db.type != DSA_DB_BRIDGE) ||
	    is_multicast_ether_addr(addr))
		return -EOPNOTSUPP;

	*hw_vid = vid ?: RTL8372N_UNAWARE_VID;
	return 0;
}

int rtl8372n_port_fdb_add(struct dsa_switch *ds, int port,
			  const unsigned char *addr, u16 vid,
			  struct dsa_db db)
{
	u16 hw_vid;
	int ret;

	ret = rtl8372n_fdb_vid(ds, port, addr, vid, db, &hw_vid);
	if (ret)
		return ret;
	return rtl837x_l2_add_static(ds->priv, addr, hw_vid, port);
}

/* A learned entry, or one that points at another port, is left alone. */
int rtl8372n_port_fdb_del(struct dsa_switch *ds, int port,
			  const unsigned char *addr, u16 vid,
			  struct dsa_db db)
{
	u16 hw_vid;
	int ret;

	ret = rtl8372n_fdb_vid(ds, port, addr, vid, db, &hw_vid);
	if (ret)
		return ret;
	return rtl837x_l2_del_static(ds->priv, addr, hw_vid, port);
}

/*
 * Report the port's unicast entries as the table holds them: VID 1 stays 1,
 * and an SVL entry, which carries a FID, is reported with VID 0. A port
 * outside an offloaded bridge learns nothing and gets no FDB entry, so it
 * is not walked: the walk method has not run on hardware yet. Dumps run
 * under rtnl, as bridge join and leave do.
 */
int rtl8372n_port_fdb_dump(struct dsa_switch *ds, int port,
			   dsa_fdb_dump_cb_t *cb, void *data)
{
	struct rtl837x_l2_uc_entry entry;
	u16 index;
	int ret;

	if (!dsa_port_bridge_dev_get(dsa_to_port(ds, port)))
		return 0;

	/* Each step starts past the last hit, so the walk ends by 4095. */
	for (index = 0; ; index++) {
		ret = rtl837x_l2_next_uc(ds->priv, &index, port, &entry);
		if (ret)
			return ret == -ENOENT ? 0 : ret;
		ret = cb(entry.mac, entry.ivl ? entry.vid : 0, entry.is_static,
			 data);
		if (ret)
			return ret;
	}
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
