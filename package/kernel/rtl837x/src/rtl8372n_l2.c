// SPDX-License-Identifier: GPL-2.0-only
/*
 * RTL8372N L2 forwarding controls: CIST port state, the bridge port flags
 * (learning and flooding), ageing, fast-age and the FDB, lookup-miss and
 * source-address actions, ASIC IGMP snooping, and the trapping of link-local
 * control frames to the CPU.
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
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/math.h>
#include <linux/minmax.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/time64.h>

#include "rtl837x.h"

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
	{ RTL837X_L2_NEW_SA_ACT, "new source address action" },
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

/* Try every class even after an error; return the first one. */
static int rtl8372n_port_clear_flood(struct rtl837x_priv *priv,
				     unsigned long flags, u32 bit,
				     u32 cpu_mask)
{
	unsigned int i;
	int ret = 0, err;

	for (i = 0; i < ARRAY_SIZE(rtl8372n_flood_regs); i++) {
		if (!(flags & rtl8372n_flood_regs[i].flag))
			continue;
		err = regmap_update_bits(priv->map, rtl8372n_flood_regs[i].reg,
					 bit | cpu_mask, cpu_mask);
		ret = ret ?: err;
	}
	return ret;
}

/*
 * Add user @port to, or remove it from, the flood masks of the classes in
 * @flags: BR_FLOOD unknown unicast, BR_MCAST_FLOOD unknown L2, IPv4 and IPv6
 * multicast, BR_BCAST_FLOOD broadcast. Every write also sets the CPU port's
 * bit, which no call removes. A failed call never leaves the port in more
 * masks than it asked for or found: enabling puts the port's bits back as
 * they were, disabling still clears the other classes.
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
	if (!enable)
		return rtl8372n_port_clear_flood(priv, flags, bit, cpu_mask);

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
					 bit | cpu_mask, bit | cpu_mask);
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
 * The bridge refuses a flag change this rejects with -EINVAL. -EOPNOTSUPP
 * would let it keep hairpin, isolated, locked, MAB or multicast-to-unicast
 * in software only while the switch forwarded as before. DSA also asks the
 * CPU and unused ports whether learning can be configured; they answer yes
 * (it stays off there), so that DSA fast-ages a port only when it learned.
 */
int rtl8372n_port_pre_bridge_flags(struct dsa_switch *ds, int port,
				   struct switchdev_brport_flags flags,
				   struct netlink_ext_ack *extack)
{
	if (!dsa_is_user_port(ds, port))
		return 0;
	if (flags.mask & ~RTL8372N_BRIDGE_FLAGS) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Only learning and flood flags are offloaded");
		return -EINVAL;
	}
	return 0;
}

/*
 * Learning and flooding follow the bridge flags only while the port is in
 * the offloaded bridge; a standalone port neither learns nor receives
 * flooded frames, whatever DSA's standalone defaults (flooding on) say. DSA
 * calls this once per flag when a port joins (dp->bridge already set) and
 * after it leaves (dp->bridge clear), each time with BR_PORT_LOCKED as well.
 * Clearing an unsupported flag is a no-op; setting one is refused. Bits are
 * cleared before any is set.
 */
int rtl8372n_port_bridge_flags(struct dsa_switch *ds, int port,
			       struct switchdev_brport_flags flags,
			       struct netlink_ext_ack *extack)
{
	unsigned long on, off;
	bool bridged;
	int ret;

	if (!dsa_is_user_port(ds, port))
		return 0;
	if (flags.mask & flags.val & ~RTL8372N_BRIDGE_FLAGS) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Only learning and flood flags are offloaded");
		return -EINVAL;
	}

	bridged = dsa_port_bridge_dev_get(dsa_to_port(ds, port));
	on = bridged ? flags.mask & flags.val : 0;
	if (flags.mask & BR_LEARNING) {
		ret = rtl8372n_port_set_learning(ds, port, on & BR_LEARNING);
		if (ret)
			return ret;
	}
	off = flags.mask & ~on & RTL8372N_FLOOD_FLAGS;
	if (off) {
		ret = rtl8372n_port_set_flood(ds, port, off, false);
		if (ret)
			return ret;
	}
	on &= RTL8372N_FLOOD_FLAGS;
	if (on) {
		ret = rtl8372n_port_set_flood(ds, port, on, true);
		if (ret)
			return ret;
	}
	dev_dbg(ds->dev, "port %d: bridge flags %#lx/%#lx, bridged %d\n",
		port, flags.val, flags.mask, bridged);
	return 0;
}

/*
 * The ASIC has no listening state, so LISTENING blocks, as in MNeroba#2; an
 * unknown state blocks too.
 */
enum rtl8372n_stp_state rtl8372n_stp_from_br(u8 state)
{
	switch (state) {
	case BR_STATE_DISABLED:
		return RTL8372N_STP_DISABLED;
	case BR_STATE_LEARNING:
		return RTL8372N_STP_LEARNING;
	case BR_STATE_FORWARDING:
		return RTL8372N_STP_FORWARDING;
	case BR_STATE_LISTENING:
	case BR_STATE_BLOCKING:
	default:
		return RTL8372N_STP_BLOCKING;
	}
}

/*
 * Besides bridged ports, DSA sets standalone ports FORWARDING on open and
 * DISABLED on stop, the CPU port FORWARDING after setup and DISABLED at
 * teardown, and unused ports DISABLED. A user port whose unlink failed
 * (rtl8372n.c) may still be linked to ports it must not reach, so it is
 * held DISABLED: the unlink is retried first, and while it still fails the
 * port gets DISABLED whatever DSA asks, including the FORWARDING that DSA
 * writes right after a bridge leave. The same retry puts right a port left
 * in an unknown or wrong VLAN mode (rtl8372n_port_isolation_held()).
 */
void rtl8372n_port_stp_state_set(struct dsa_switch *ds, int port, u8 state)
{
	struct rtl837x_priv *priv = ds->priv;
	enum rtl8372n_stp_state hw;
	int ret;

	if (state > BR_STATE_BLOCKING)
		dev_warn_ratelimited(priv->dev,
				     "port %d: unknown STP state %u, blocking\n",
				     port, state);
	hw = rtl8372n_stp_from_br(state);
	if (rtl8372n_port_isolation_held(ds, port)) {
		dev_err_ratelimited(priv->dev,
				    "port %d: may still be linked, STP state %u refused, held DISABLED\n",
				    port, state);
		hw = RTL8372N_STP_DISABLED;
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

/* Only user ports ever learn; there is nothing to flush on the others. */
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
 * a user port or on the CPU port, which holds the bridge's own addresses
 * and, with assisted learning, those the bridge learned on foreign ports.
 * An add rewrites the entry of its key in place, a learned one included
 * (rtl837x_l2_add_static() reads back the port and static bit), so a
 * station that roams from a LAN port to Wi-Fi moves to the CPU port at
 * once. A delete removes only a static entry still on its port, so the
 * entry learned on a LAN port after a station roams back stays.
 * DSA uses VID 0 for a VLAN-unaware bridge, whose frames the switch puts in
 * VLAN 1, so DSA VIDs 0 and 1 share one entry (see rtl8372n_fdb_alias).
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

/*
 * DSA tracks VIDs 0 and 1 apart (the bridge holds its own addresses at
 * both, and DSA refcounts CPU-port addresses per VID), while the switch has
 * one entry {MAC, VLAN 1} for both. One record per MAC with such an entry
 * keeps the port that holds each of the two DSA VIDs, so that deleting one
 * leaves the entry to the other. The entry is written for the port of the
 * DSA VID added last while that one is held, else for the other's port,
 * and removed once neither is held. As in the bridge FDB, a DSA VID is held
 * by one port at a time: an add for another port moves it (a `bridge fdb
 * replace` sends no delete for the old port). Protected by
 * rtl837x_priv.fdb_lock; a record lives while one of its VIDs is held.
 */
struct rtl8372n_fdb_alias {
	struct list_head list;
	u8 mac[ETH_ALEN];
	/* The port holding DSA VID 0 and DSA VID 1, -1 for none. */
	s8 port[2];
	/* The port the entry was last written for, -1 if unknown. */
	s8 hw_port;
	/* The DSA VID, 0 or 1, added last. */
	u8 last;
};

static struct rtl8372n_fdb_alias *
rtl8372n_fdb_alias_find(struct rtl837x_priv *priv, const unsigned char *mac)
{
	struct rtl8372n_fdb_alias *alias;

	lockdep_assert_held(&priv->fdb_lock);

	list_for_each_entry(alias, &priv->fdb_aliases, list)
		if (ether_addr_equal(alias->mac, mac))
			return alias;
	return NULL;
}

/* The port the entry is for, -1 once neither DSA VID is held. */
static int rtl8372n_fdb_alias_want(const struct rtl8372n_fdb_alias *alias)
{
	if (alias->port[alias->last] >= 0)
		return alias->port[alias->last];
	return alias->port[!alias->last];
}

/*
 * Bring the entry in line with the record: write it for the port that
 * should hold it unless it was last written so, or, once neither DSA VID
 * is held, remove it if it is still the static entry of @gone, the port
 * that let go last. A failed write leaves the entry unknown, so the next
 * call writes it again.
 */
static int rtl8372n_fdb_alias_sync(struct rtl837x_priv *priv,
				   struct rtl8372n_fdb_alias *alias, int gone)
{
	int want = rtl8372n_fdb_alias_want(alias);
	int ret;

	if (want < 0) {
		alias->hw_port = -1;
		return rtl837x_l2_del_static(priv, alias->mac,
					     RTL8372N_UNAWARE_VID, gone);
	}
	if (alias->hw_port == want)
		return 0;
	ret = rtl837x_l2_add_static(priv, alias->mac, RTL8372N_UNAWARE_VID,
				    want);
	alias->hw_port = ret ? -1 : want;
	return ret;
}

static void rtl8372n_fdb_alias_put(struct rtl8372n_fdb_alias *alias)
{
	if (alias->port[0] < 0 && alias->port[1] < 0) {
		list_del(&alias->list);
		kfree(alias);
	}
}

/* Hold DSA VID @vid (0 or 1) of @mac on @port. */
static int rtl8372n_fdb_alias_add(struct rtl837x_priv *priv, int port,
				  const unsigned char *mac, u16 vid)
{
	struct rtl8372n_fdb_alias *alias;
	int ret, err, old_port;
	u8 v = !!vid, old_last;

	lockdep_assert_held(&priv->fdb_lock);

	alias = rtl8372n_fdb_alias_find(priv, mac);
	if (!alias) {
		alias = kzalloc(sizeof(*alias), GFP_KERNEL);
		if (!alias)
			return -ENOMEM;
		ether_addr_copy(alias->mac, mac);
		alias->port[0] = -1;
		alias->port[1] = -1;
		alias->hw_port = -1;
		list_add(&alias->list, &priv->fdb_aliases);
	}

	old_port = alias->port[v];
	old_last = alias->last;
	alias->port[v] = port;
	alias->last = v;
	ret = rtl8372n_fdb_alias_sync(priv, alias, port);
	if (ret) {
		/*
		 * Undo the add; a write that failed may still have landed,
		 * so put the entry back for the holders left, or remove it
		 * if there are none.
		 */
		alias->port[v] = old_port;
		alias->last = old_last;
		err = rtl8372n_fdb_alias_sync(priv, alias, port);
		if (err)
			dev_err(priv->dev,
				"L2 %pM vid %u: entry not restored after a failed add: %d\n",
				mac, RTL8372N_UNAWARE_VID, err);
	}
	rtl8372n_fdb_alias_put(alias);
	return ret;
}

/*
 * Release DSA VID @vid (0 or 1) of @mac from @port. On the CPU port a failed
 * delete keeps the hold, as DSA keeps the address then (it resets its
 * refcount to 1 and deletes it again later). A user port has no refcount in
 * DSA, and the bridge has dropped its entry already: nothing will delete it
 * again, so the hold goes whatever the hardware did. The static entry the
 * switch may still hold for that port is logged, and the record no longer
 * writes it back; the next operation on @mac rewrites or removes it.
 */
static int rtl8372n_fdb_alias_del(struct rtl837x_priv *priv, int port,
				  const unsigned char *mac, u16 vid)
{
	struct rtl8372n_fdb_alias *alias;
	u8 v = !!vid;
	int ret;

	lockdep_assert_held(&priv->fdb_lock);

	alias = rtl8372n_fdb_alias_find(priv, mac);
	/* Never held, e.g. after a failed add: remove @port's entry if any. */
	if (!alias)
		return rtl837x_l2_del_static(priv, mac, RTL8372N_UNAWARE_VID,
					     port);
	/*
	 * Held elsewhere only: @port releases nothing, and the entry stays
	 * for the holders, written again if unknown. A failure there is not
	 * this delete's: reporting it would make DSA keep, on the CPU port,
	 * an address that the record never gave to it.
	 */
	if (alias->port[v] != port) {
		ret = rtl8372n_fdb_alias_sync(priv, alias, port);
		if (ret)
			dev_err(priv->dev,
				"L2 %pM vid %u: entry for port %d not restored: %d\n",
				mac, RTL8372N_UNAWARE_VID,
				rtl8372n_fdb_alias_want(alias), ret);
		return 0;
	}

	alias->port[v] = -1;
	ret = rtl8372n_fdb_alias_sync(priv, alias, port);
	if (ret && dsa_is_cpu_port(priv->ds, port))
		alias->port[v] = port;
	else if (ret)
		dev_err(priv->dev,
			"L2 %pM vid %u: DSA vid %u released by port %d, which may keep a static entry: %d\n",
			mac, RTL8372N_UNAWARE_VID, vid, port, ret);
	else
		dev_dbg(priv->dev,
			"L2 %pM: DSA vid %u released by port %d, entry for port %d\n",
			mac, vid, port, rtl8372n_fdb_alias_want(alias));
	rtl8372n_fdb_alias_put(alias);
	return ret;
}

/* The L2 table is empty after the setup clear, and gone after teardown. */
void rtl8372n_fdb_reset(struct rtl837x_priv *priv)
{
	struct rtl8372n_fdb_alias *alias, *tmp;

	mutex_lock(&priv->fdb_lock);
	list_for_each_entry_safe(alias, tmp, &priv->fdb_aliases, list) {
		list_del(&alias->list);
		kfree(alias);
	}
	mutex_unlock(&priv->fdb_lock);
}

int rtl8372n_port_fdb_add(struct dsa_switch *ds, int port,
			  const unsigned char *addr, u16 vid,
			  struct dsa_db db)
{
	struct rtl837x_priv *priv = ds->priv;
	u16 hw_vid;
	int ret;

	ret = rtl8372n_fdb_vid(ds, port, addr, vid, db, &hw_vid);
	if (ret)
		return ret;

	mutex_lock(&priv->fdb_lock);
	if (hw_vid == RTL8372N_UNAWARE_VID)
		ret = rtl8372n_fdb_alias_add(priv, port, addr, vid);
	else
		ret = rtl837x_l2_add_static(priv, addr, hw_vid, port);
	mutex_unlock(&priv->fdb_lock);
	return ret;
}

/* A learned entry, or one that points at another port, is left alone. */
int rtl8372n_port_fdb_del(struct dsa_switch *ds, int port,
			  const unsigned char *addr, u16 vid,
			  struct dsa_db db)
{
	struct rtl837x_priv *priv = ds->priv;
	u16 hw_vid;
	int ret;

	ret = rtl8372n_fdb_vid(ds, port, addr, vid, db, &hw_vid);
	if (ret)
		return ret;

	mutex_lock(&priv->fdb_lock);
	if (hw_vid == RTL8372N_UNAWARE_VID)
		ret = rtl8372n_fdb_alias_del(priv, port, addr, vid);
	else
		ret = rtl837x_l2_del_static(priv, addr, hw_vid, port);
	mutex_unlock(&priv->fdb_lock);
	return ret;
}

/*
 * Report the port's unicast entries as the table holds them: VID 1 stays 1,
 * and an SVL entry, which carries a FID, is reported with VID 0. A port
 * outside an offloaded bridge learns nothing and gets no FDB entry, so it
 * is not walked: the walk method has not run on hardware yet. A walk that
 * meets an entry it cannot place (status bit 13, see rtl837x_l2_next_uc())
 * ends there, logged, and the dump is short. Dumps run under rtnl, as
 * bridge join and leave do.
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
 * a frame whose source address is new, or known on another port, is
 * forwarded, so a new or moving station is never dropped. ASIC IGMP/MLD
 * snooping stays off: the bridge owns multicast and no MDB is offloaded.
 *
 * Bridging fills the one L2 table that standalone and software-bridged
 * ports share, and it turns learning off on ports, so four actions whose
 * reset values P0 never needed are written too:
 * - a destination learned on a port outside the ingress port's isolation
 *   mask is handled as unknown, so it floods within that mask (the CPU alone
 *   for a standalone port) instead of the undocumented "NOP", which may
 *   forward across isolation;
 * - a new source address on a port at its learning limit, which with limit
 *   0 is every frame of a port that does not learn, is forwarded without
 *   learning, not dropped or trapped (a trapped frame comes with a reason
 *   the tagger does not know, and the software bridge would not forward it);
 * - a new source address whose bucket and overflow CAM are full (a host
 *   flooding random source addresses fills them) is forwarded without
 *   learning, for the same reasons, instead of black-holing the stations
 *   that arrive while the table is full;
 * - no user port sends a frame back out of the port it came in on.
 * Whether a known source address may move to another port (0x535c) is left
 * at its reset value: the SDK's description of the bit contradicts its
 * name, so only the readback records it.
 *
 * P0 wrote none of these, and its standalone ports worked with their reset
 * values: a standalone port's isolation mask holds the CPU alone, so what
 * these actions forward from it only ever reaches the CPU. Only bridge
 * offload relies on the values written here.
 */
static int rtl8372n_l2_pin_actions(struct dsa_switch *ds)
{
	struct rtl837x_priv *priv = ds->priv;
	u32 act_mask, act;
	int i, ret;

	rtl8372n_port_act(ds, &act_mask, &act);
	for (i = 0; i < ARRAY_SIZE(rtl8372n_port_act_regs); i++) {
		ret = regmap_update_bits(priv->map, rtl8372n_port_act_regs[i].reg,
					 act_mask, act);
		if (ret)
			return ret;
	}

	ret = rtl837x_reg_bits_write(priv, RTL837X_L2_CTRL,
				     RTL837X_L2_CTRL_LOOKUP_HIT_ISO_UNKNOWN, 1);
	if (ret)
		return ret;
	ret = rtl837x_reg_bits_write(priv, RTL837X_L2_CTRL,
				     RTL837X_L2_CTRL_HASH_FULL_ACT,
				     RTL837X_L2_ACT_FORWARD);
	if (ret)
		return ret;
	ret = rtl837x_reg_bits_write(priv, RTL837X_L2_LEARN_LIMIT_ACT,
				     RTL837X_L2_LEARN_LIMIT_ACT_MASK,
				     RTL837X_L2_ACT_FORWARD);
	if (ret)
		return ret;
	ret = regmap_clear_bits(priv->map, RTL837X_L2_SRC_PORT_PERMIT,
				dsa_user_ports(ds));
	if (ret)
		return ret;

	return rtl837x_reg_bits_write(priv, RTL837X_IGMP_CTRL,
				      RTL837X_IGMP_CTRL_SNOOPING, 0);
}

/*
 * A failure to pin the actions above turns bridge offload off instead of
 * failing setup: standalone ports then forward as in P0. The STP states
 * are written whatever happens, and a failure there fails setup, since
 * standalone ports need FORWARDING.
 */
int rtl8372n_l2_setup(struct dsa_switch *ds)
{
	struct rtl837x_priv *priv = ds->priv;
	int port, ret;

	ret = rtl8372n_l2_pin_actions(ds);
	if (ret) {
		dev_warn(priv->dev, "L2 actions not pinned: %d\n", ret);
		rtl8372n_disable_offload(priv, "L2 actions not pinned");
	}

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

/*
 * Setup readback lines for the controls above, in the P0 readback format.
 * The STP states are what standalone ports need; the actions and the trap
 * are what only bridge offload relies on, all of SDK-only lineage: a failed
 * read or a mismatch of one of them turns offload off. In particular the
 * tagger patch 0457 leaves link-local frames unmarked on the premise that
 * the switch traps them, so a trap that does not read back must keep every
 * bridge in software.
 */
void rtl8372n_l2_report_readback(struct dsa_switch *ds)
{
	struct rtl837x_priv *priv = ds->priv;
	u32 act_mask, act, stp_mask = 0, stp = 0;
	bool actions = true, trap = true;
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
		actions &= rtl8372n_report_p0_reg(priv,
						  rtl8372n_port_act_regs[i].name,
						  rtl8372n_port_act_regs[i].reg,
						  act_mask, act);
	/* Whole-register lines: the other fields show their reset values. */
	actions &= rtl8372n_report_p0_reg(priv, "IGMP/MLD snooping",
					  RTL837X_IGMP_CTRL,
					  RTL837X_IGMP_CTRL_SNOOPING, 0);

	trap &= rtl8372n_report_p0_reg(priv, "RMA/PTP trap CPU",
				       RTL837X_RMA_PTP_TRAP_CTRL,
				       RTL837X_RMA_PTP_TRAP_CPU,
				       FIELD_PREP(RTL837X_RMA_PTP_TRAP_CPU,
						  RTL837X_TRAP_CPU_EXTERNAL));
	trap &= rtl8372n_report_p0_reg(priv, "L2 trap CPU", RTL837X_L2_CTRL,
				       RTL837X_L2_CTRL_TRAP_CPU,
				       FIELD_PREP(RTL837X_L2_CTRL_TRAP_CPU,
						  RTL837X_TRAP_CPU_EXTERNAL));
	actions &= rtl8372n_report_p0_reg(priv,
					  "L2 lookup-hit isolation action",
					  RTL837X_L2_CTRL,
					  RTL837X_L2_CTRL_LOOKUP_HIT_ISO_UNKNOWN,
					  RTL837X_L2_CTRL_LOOKUP_HIT_ISO_UNKNOWN);
	actions &= rtl8372n_report_p0_reg(priv, "L2 hash-full action",
					  RTL837X_L2_CTRL,
					  RTL837X_L2_CTRL_HASH_FULL_ACT,
					  FIELD_PREP(RTL837X_L2_CTRL_HASH_FULL_ACT,
						     RTL837X_L2_ACT_FORWARD));
	actions &= rtl8372n_report_p0_reg(priv, "learn-limit action",
					  RTL837X_L2_LEARN_LIMIT_ACT,
					  RTL837X_L2_LEARN_LIMIT_ACT_MASK,
					  RTL837X_L2_ACT_FORWARD);
	actions &= rtl8372n_report_p0_reg(priv, "source port permit",
					  RTL837X_L2_SRC_PORT_PERMIT,
					  dsa_user_ports(ds), 0);
	/* Never written, polarity unknown: the value only, for the bench. */
	rtl8372n_report_p0_value(priv, "SA-moving forbid",
				 RTL837X_L2_SA_MOVING_FORBID);
	for (slot = 0; slot < RTL837X_RMA_LINK_LOCAL_SLOTS; slot++) {
		if (slot == RTL837X_RMA_SLOT_PAUSE)
			continue;
		trap &= rtl8372n_report_p0_reg(priv, "RMA link-local trap",
					       RTL837X_RMA_CTRL(slot),
					       RTL8372N_RMA_TRAP_FIELDS,
					       RTL8372N_RMA_TRAP_VALUE);
	}
	trap &= rtl8372n_report_p0_reg(priv, "RMA LLDP trap",
				       RTL837X_RMA_CTRL_LLDP,
				       RTL8372N_RMA_TRAP_FIELDS,
				       RTL8372N_RMA_TRAP_VALUE);
	trap &= rtl8372n_report_p0_reg(priv, "RMA LLDP_EN", RTL837X_RMA_CFG,
				       RTL837X_RMA_CFG_LLDP_EN, 0);

	if (!actions)
		rtl8372n_disable_offload(priv, "L2 action readback mismatch");
	if (!trap)
		rtl8372n_disable_offload(priv, "trap readback mismatch");
}
