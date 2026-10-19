// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025 StarField Xu <air_jinkela@163.com>
 */
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/ethtool.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/regmap.h>
#include <linux/of_net.h>

#include "rtl837x.h"

#define RTL8372N_MDIO_RESET_DELAY_US 10

#define IS_SERDES_PORT(port) (((port) == 3) || ((port) == 8))

#define PORT_TO_SERDES_IDX(port) ((port) == 3 ? 0 : 1)

struct rtl8372n_pcs {
	struct phylink_pcs pcs;
	struct rtl837x_priv *priv;
	int index;
};

struct rtl8372n {
	struct rtl8372n_pcs pcs[RTL8372N_NUM_PORTS];
};

static int rtl8372n_detect(struct rtl837x_priv *priv)
{
	u32 chip_id;
	int ret;

	ret = rtl837x_reg_read(priv, RTL837X_REG_CHIP_ID, &chip_id);
	if (ret)
		return ret;

	dev_info(priv->dev, "chip ID: 0x%08x\n", chip_id);
	if ((chip_id >> 8) != 0x837270)
		return -ENODEV;

	priv->num_ports = RTL8372N_NUM_PORTS;
	return 0;
}

static int rtl8372n_soft_reset_chip(struct rtl837x_priv *priv)
{
	u32 tmp;
	int ret;

	ret = priv->write_reset(priv);
	if (ret)
		return ret;

	msleep(250);
	return regmap_read_poll_timeout(priv->map, RTL837X_REG_RESET, tmp,
					!(tmp & RTL837X_RESET_SOC),
					20000, 1000000);

}

static enum dsa_tag_protocol rtl8372n_get_tag_protocol(struct dsa_switch *ds,
							int port,
							enum dsa_tag_protocol mp)
{
	return DSA_TAG_PROTO_RTL8_4;
}

static int rtl8372n_mdio_phy_read_c45(struct mii_bus *bus, int port, int devad, int regnum)
{
	struct rtl837x_priv *priv = bus->priv;
	u16 val;
	int ret = priv->ops->phy_read_c45(priv, port, devad, regnum, &val);

	if (ret) {
		dev_err_ratelimited(priv->dev,
				    "PHY C45 read failed: port=%d devad=%d reg=0x%04x err=%d\n",
				    port, devad, regnum, ret);
		return ret;
	}

	return val;
}

static int rtl8372n_mdio_phy_write_c45(struct mii_bus *bus, int port, int devad, int regnum, u16 val)
{
	struct rtl837x_priv *priv = bus->priv;

	return priv->ops->phy_write_c45(priv, port, devad, regnum, val);
}

static int rtl8372n_register_phys(struct rtl837x_priv *priv,
				  struct mii_bus *bus, struct device_node *mnp)
{
	struct device_node *child;
	struct phy_device *phy;
	u32 seen = 0;
	int port, addr, ret;

	/* This private bus exposes only enabled internal user PHYs. */
	for_each_available_child_of_node(mnp, child) {
		if (of_node_name_eq(child, "ethernet-phy-package")) {
			dev_err(priv->dev,
				"PHY packages are not supported on the private MDIO bus\n");
			of_node_put(child);
			return -EOPNOTSUPP;
		}
		addr = of_mdio_parse_addr(priv->dev, child);
		if (addr < 0 || !of_mdiobus_child_is_phy(child) ||
		    !(BIT(addr) & dsa_user_ports(priv->ds) & RTL8372N_PHY_PORT_MASK) ||
		    (seen & BIT(addr))) {
			of_node_put(child);
			return -EINVAL;
		}
		seen |= BIT(addr);
	}
	if (mnp && seen != (dsa_user_ports(priv->ds) & RTL8372N_PHY_PORT_MASK)) {
		dev_err(priv->dev,
			"MDIO node must describe every enabled internal PHY at ports 4-7\n");
		return -EINVAL;
	}

	for (port = 4; port <= 7; port++) {
		if (!(dsa_user_ports(priv->ds) & BIT(port)))
			continue;

		/*
		 * The switch reaches its internal PHYs through an MMD-addressed
		 * access engine, so register them as the Clause 45 devices they
		 * are and let phylib bind the matching driver by ID (the RTL8224
		 * entry of the Realtek PHY driver).
		 */
		phy = get_phy_device(bus, port, true);
		if (IS_ERR(phy)) {
			ret = PTR_ERR(phy);
			dev_err(priv->dev, "PHY discovery port %d failed: %d\n", port, ret);
			return ret;
		}
		/*
		 * This bus has no Clause 22 accessors. Drop the "C22 registers
		 * present" bit so that phylib's generic aneg restart and
		 * fallbacks stay on the C45 path.
		 */
		phy->c45_ids.devices_in_package &= ~BIT(0);

		for_each_available_child_of_node(mnp, child) {
			if (of_mdio_parse_addr(priv->dev, child) == port)
				break;
		}
		if (child)
			ret = of_mdiobus_phy_device_register(bus, phy, child, port);
		else
			ret = phy_device_register(phy);
		of_node_put(child);
		if (ret) {
			phy_device_free(phy);
			dev_err(priv->dev, "PHY registration port %d failed: %d\n", port, ret);
			return ret;
		}
		/* Registered PHYs are removed and freed by managed bus teardown. */
	}
	return 0;
}

static int rtl8372n_setup_mdio(struct rtl837x_priv *priv)
{
	struct device_node *np = priv->dev->of_node;
	struct device_node *mnp;
	struct dsa_switch *ds = priv->ds;
	struct device *dev = priv->dev;
	struct mii_bus *bus;
	struct phy_device *phy;
	int port, ret = 0;

	mnp = of_get_child_by_name(np, "mdio");

	if (mnp && !of_device_is_available(mnp)) {
		ret = -ENODEV;
		goto out;
	}

	bus = devm_mdiobus_alloc(dev);
	if (!bus) {
		ret = -ENOMEM;
		goto out;
	}

	/* DSA falls back to this bus for user ports without a phy-handle. */
	ds->user_mii_bus = bus;

	bus->priv = priv;
	bus->name = KBUILD_MODNAME "-mii";
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s", dev_name(dev));
	bus->read_c45 = rtl8372n_mdio_phy_read_c45;
	bus->write_c45 = rtl8372n_mdio_phy_write_c45;
	bus->parent = dev;
	/* Internal PHYs are registered explicitly, for enabled ports only. */
	bus->phy_mask = ~0;
	if (mnp) {
		device_set_node(&bus->dev, of_fwnode_handle(mnp));
		/* Match the default used by __of_mdiobus_register(). */
		bus->reset_delay_us = RTL8372N_MDIO_RESET_DELAY_US;
		of_property_read_u32(mnp, "reset-delay-us", &bus->reset_delay_us);
		of_property_read_u32(mnp, "reset-post-delay-us", &bus->reset_post_delay_us);
	}

	ret = devm_mdiobus_register(dev, bus);
	if (!ret)
		ret = rtl8372n_register_phys(priv, bus, mnp);
	if (!ret) {
		for (port = 4; port <= 7; port++) {
			if (!(dsa_user_ports(ds) & BIT(port)))
				continue;
			phy = mdiobus_get_phy(bus, port);
			if (!phy) {
				dev_err(dev, "port %d: PHY device absent\n", port);
				ret = -ENODEV;
				continue;
			}
			/*
			 * phy->drv survives a failed phylib probe, so check the
			 * binding itself. Unbound, phylib falls back to genphy.
			 */
			device_lock(&phy->mdio.dev);
			if (!device_is_bound(&phy->mdio.dev))
				dev_warn(dev,
					 "port %d: no PHY driver bound to C45 ID %#010x (probe failed or kmod-phy-realtek missing)\n",
					 port, phy->c45_ids.device_ids[MDIO_MMD_PMAPMD]);
			else
				dev_dbg(dev, "port %d: PHY driver %s\n", port,
					phy->drv->name);
			device_unlock(&phy->mdio.dev);
			/*
			 * The MACs implement no LPI, so phylink leaves EEE alone.
			 * Keep it off, as the SDK-based driver did, instead of
			 * advertising the PHY's reset default.
			 */
			phy_disable_eee(phy);
		}
	}
	if (ret) {
		ds->user_mii_bus = NULL;
		dev_err(dev, "failed to register MDIO bus: %d\n", ret);
	}

out:
	of_node_put(mnp);
	return ret;
}

static unsigned int rtl8372n_sds_pcs_inband_caps(struct phylink_pcs *pcs,
						 phy_interface_t interface)
{
	struct rtl8372n_pcs *p = container_of(pcs, struct rtl8372n_pcs, pcs);

	if ((p->index == 3 || p->index == 8) &&
	    interface == PHY_INTERFACE_MODE_10GBASER)
		return LINK_INBAND_DISABLE;

	return 0;
}

static void rtl8372n_sds_pcs_get_state(struct phylink_pcs *pcs,
				     unsigned int neg_mode,
				     struct phylink_link_state *state)
{
	struct rtl8372n_pcs *p = container_of(pcs, struct rtl8372n_pcs, pcs);
	struct rtl837x_priv *priv = p->priv;
	u32 link, duplex, rx_pause, tx_pause, speed;
	int port = p->index;

	/* Never expose a partially read snapshot after a transport failure. */
	state->link = false;
	state->an_complete = false;
	state->speed = SPEED_UNKNOWN;
	state->duplex = DUPLEX_UNKNOWN;
	state->pause &= ~(MLO_PAUSE_RX | MLO_PAUSE_TX);
	if (rtl837x_reg_bits_read(priv, RTL837X_MAC_LINK_STATUS,
				 BIT(port), &link) || !link)
		return;
	if (rtl837x_reg_bits_read(priv, RTL837X_MAC_DUPLEX_STATUS,
				 BIT(port), &duplex) ||
	    rtl837x_reg_bits_read(priv, RTL837X_MAC_RX_PAUSE_STATUS,
				 BIT(port), &rx_pause) ||
	    rtl837x_reg_bits_read(priv, RTL837X_MAC_TX_PAUSE_STATUS,
				 BIT(port), &tx_pause) ||
	    rtl837x_reg_bits_read(priv, RTL837X_MAC_SPEED_STATUS_REG(port),
				 RTL837X_MAC_SPEED_STATUS_MASK(port), &speed))
		return;

	/* P0 exposes this PCS only for a fixed 10GBASE-R CPU link. */
	if (speed != 4 || !duplex)
		return;
	state->speed = SPEED_10000;
	state->duplex = DUPLEX_FULL;
	if (rx_pause)
		state->pause |= MLO_PAUSE_RX;
	if (tx_pause)
		state->pause |= MLO_PAUSE_TX;
	state->an_complete = true;
	state->link = true;
}

static int rtl8372n_sds_pcs_config(struct phylink_pcs *pcs,
				   unsigned int neg_mode,
				   phy_interface_t interface,
				   const unsigned long *advertising,
				   bool permit_pause_to_mac)
{
	struct rtl8372n_pcs *p = container_of(pcs, struct rtl8372n_pcs, pcs);
	struct rtl837x_priv *priv = p->priv;
	u32 mask;

	if (interface != PHY_INTERFACE_MODE_10GBASER)
		return -EOPNOTSUPP;
	if (p->index != 3 && p->index != 8)
		return -EINVAL;

	mask = p->index == 3 ? RTL837X_SDS0_MODE_MASK : RTL837X_SDS1_MODE_MASK;
	return rtl837x_reg_bits_write(priv, RTL837X_SDS_MODE_SELECT, mask,
				      RTL837X_SDS_MODE_10GBASE_R);
}

static void rtl8372n_sds_pcs_link_up(struct phylink_pcs *pcs, unsigned int neg_mode,
			    phy_interface_t interface, int speed, int duplex)
{
	struct rtl8372n_pcs *_pcs = container_of(pcs, struct rtl8372n_pcs, pcs);
	struct rtl837x_priv *priv = _pcs->priv;
	int port = _pcs->index;

	dev_dbg(priv->dev, "[%s]PCS link up serdes (%d) mode (%s)\n", __func__,
			  PORT_TO_SERDES_IDX(port),
			  phy_modes(interface));
}

static const struct phylink_pcs_ops rtl8372n_sds_pcs_ops = {
	.pcs_inband_caps = rtl8372n_sds_pcs_inband_caps,
	.pcs_get_state = rtl8372n_sds_pcs_get_state,
	.pcs_config = rtl8372n_sds_pcs_config,
	.pcs_link_up = rtl8372n_sds_pcs_link_up,
};

static void rtl8372n_phylink_get_caps(struct dsa_switch *ds, int port,
				       struct phylink_config *config)
{
	if (IS_SERDES_PORT(port)) {
		__set_bit(PHY_INTERFACE_MODE_10GBASER,
			  config->supported_interfaces);
		config->mac_capabilities = MAC_10000FD |
			MAC_SYM_PAUSE | MAC_ASYM_PAUSE;
	} else if (port >= 4 && port <= 7) {
		__set_bit(PHY_INTERFACE_MODE_INTERNAL,
			  config->supported_interfaces);
		config->mac_capabilities = MAC_2500FD | MAC_1000 | MAC_100 |
			MAC_10 | MAC_SYM_PAUSE | MAC_ASYM_PAUSE;
	}
}

static struct phylink_pcs *rtl8372n_phylink_mac_select_pcs(struct phylink_config *config,
						phy_interface_t interface)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct rtl837x_priv *priv = dp->ds->priv;
	struct rtl8372n *chip_data = priv->chip_data;

	if (IS_SERDES_PORT(dp->index))
		return &(chip_data->pcs[dp->index].pcs);
	return NULL;
}

static void rtl8372n_phylink_mac_config(struct phylink_config *config, unsigned int mode,
			const struct phylink_link_state *state)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct rtl837x_priv *priv = dp->ds->priv;

	if (!IS_SERDES_PORT(dp->index))
		return;
	dev_dbg(priv->dev, "[%s]: port:%d interface:%s speed:%s duplex:%s advertising:%*pb pause:0x%x\n", __func__,
			   dp->index, phy_modes(state->interface), phy_speed_to_str(state->speed),
			   phy_duplex_to_str(state->duplex), __ETHTOOL_LINK_MODE_MASK_NBITS, state->advertising, state->pause);
}

static void rtl8372n_phylink_mac_link_down(struct phylink_config *config, unsigned int mode,
				phy_interface_t interface)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct rtl837x_priv *priv = dp->ds->priv;

	if (!IS_SERDES_PORT(dp->index))
		return;

	dev_dbg(priv->dev, "[%s]: port:%d interface:%s\n", __func__,
			   dp->index, phy_modes(interface));
}

static void rtl8372n_phylink_mac_link_up(struct phylink_config *config,
			struct phy_device *phy, unsigned int mode,
			phy_interface_t interface, int speed, int duplex,
			bool tx_pause, bool rx_pause)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct rtl837x_priv *priv = dp->ds->priv;

	if (!IS_SERDES_PORT(dp->index))
		return;
	dev_dbg(priv->dev,
			   "[%s]: port:%d mode:%s speed:%s duplex:%s tx_pause:%d rx_pause:%d\n", __func__,
			   dp->index, phy_modes(interface), phy_speed_to_str(speed),
			   phy_duplex_to_str(duplex), tx_pause, rx_pause
			);
}

static const struct phylink_mac_ops rtl8372n_phylink_mac_ops = {
	.mac_select_pcs	= rtl8372n_phylink_mac_select_pcs,
	.mac_config	= rtl8372n_phylink_mac_config,
	.mac_link_down	= rtl8372n_phylink_mac_link_down,
	.mac_link_up	= rtl8372n_phylink_mac_link_up,
};

static int rtl8372n_set_tag_rtl(struct dsa_switch *ds)
{
	int ret;
	struct rtl837x_priv *priv = ds->priv;
	struct dsa_port *dp, *cpu_dp = NULL;

	/* This P0 implementation supports exactly one CPU port. */
	dsa_switch_for_each_cpu_port(dp, ds) {
		if (cpu_dp)
			return -EOPNOTSUPP;
		cpu_dp = dp;
	}

	if (!cpu_dp)
		return -ENODEV;

	/* Set external CPU DSA tag insert mode. */
	/*
	 *	CPU_INSERT_TO_ALL = 0,
	 *	CPU_INSERT_TO_TRAPPING,
	 *	CPU_INSERT_TO_NONE,
	 *	CPU_INSERT_END
	 */
	ret = rtl837x_reg_bits_write(priv, RTL837X_CPU_TAG_CTRL,
			  RTL837X_CPU_TAG_INSERT_MODE, 0
			);
	if (ret)
		return ret;

	/* Set external CPU port. */
	ret = rtl837x_reg_bits_write(priv, RTL837X_EXTERNAL_CPU_PORT,
			  RTL837X_EXTERNAL_CPU_PORT_MASK, cpu_dp->index
			);
	if (ret)
		return ret;

	/* Enable CPU tag. */
	ret = rtl837x_reg_bits_write(priv, RTL837X_CPU_TAG_CTRL,
			  RTL837X_CPU_TAG_ENABLE, 1
			);
	if (ret)
		return ret;

	/* Add the CPU port to the RTL8_4 tag-aware port mask. */
	ret = rtl837x_reg_bits_write(priv, RTL837X_CPU_TAG_AWARE, BIT(cpu_dp->index), 1);
	if (ret)
		return ret;

	return 0;
}

static int rtl8372n_port_set_isolation(struct rtl837x_priv *priv, int port,
					u32 mask)
{
	return rtl837x_reg_write(priv, RTL837X_PORT_ISOLATION_REG(port), mask);
}

/*
 * Close every hardware path between user @port and the other members of
 * @bridge: first the port's own mask goes back to the CPU alone, then the
 * port leaves the members' masks. Best effort; errors are logged.
 */
static void rtl8372n_port_bridge_isolate(struct dsa_switch *ds, int port,
					 const struct dsa_bridge *bridge)
{
	struct rtl837x_priv *priv = ds->priv;
	struct dsa_port *dp;
	int ret;

	ret = rtl8372n_port_set_isolation(priv, port, dsa_cpu_ports(ds));
	if (ret)
		dev_err(priv->dev, "port %d: isolation not restored: %d\n",
			port, ret);
	/* On a join rollback @port still offloads @bridge: skip it by index. */
	dsa_switch_for_each_user_port(dp, ds) {
		if (dp->index == port || !dsa_port_offloads_bridge(dp, bridge))
			continue;
		ret = regmap_update_bits(priv->map,
					 RTL837X_PORT_ISOLATION_REG(dp->index),
					 BIT(port), 0);
		if (ret)
			dev_err(priv->dev,
				"port %d: still reachable from port %d: %d\n",
				port, dp->index, ret);
	}
}

/*
 * Offload one VLAN-unaware bridge (ds->max_num_bridges = 1). Port isolation
 * is the forwarding domain: a member reaches the CPU and the other members,
 * a standalone port only the CPU. The members' masks gain the port first and
 * the port's own mask opens last, so a failure part-way leaves no path out
 * of the port. The port is held DISABLED meanwhile: that is the state DSA's
 * attribute sync writes next for a new bridge port, followed by its flags
 * (learning, flooding) and the ageing time.
 */
static int rtl8372n_port_bridge_join(struct dsa_switch *ds, int port,
				     struct dsa_bridge bridge,
				     bool *tx_fwd_offload,
				     struct netlink_ext_ack *extack)
{
	struct rtl837x_priv *priv = ds->priv;
	struct dsa_port *dp;
	u32 members = 0;
	int ret;

	if (!dsa_is_user_port(ds, port))
		return -EINVAL;
	/*
	 * -EOPNOTSUPP leaves the port standalone under the software bridge.
	 * Until the VLAN callbacks exist a VLAN-aware bridge stays in software.
	 */
	if (br_vlan_enabled(bridge.dev)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "VLAN-aware bridges are not offloaded yet");
		return -EOPNOTSUPP;
	}

	ret = rtl8372n_stp_set_state(priv, port, RTL8372N_STP_DISABLED);
	if (ret)
		goto undo;
	dsa_switch_for_each_user_port(dp, ds) {
		if (dp->index == port || !dsa_port_offloads_bridge(dp, &bridge))
			continue;
		ret = regmap_update_bits(priv->map,
					 RTL837X_PORT_ISOLATION_REG(dp->index),
					 BIT(port), BIT(port));
		if (ret)
			goto undo;
		members |= BIT(dp->index);
	}
	ret = rtl8372n_port_set_isolation(priv, port,
					  dsa_cpu_ports(ds) | members);
	if (ret)
		goto undo;

	dev_dbg(priv->dev, "port %d joined bridge %u, members %#x\n", port,
		bridge.num, members);
	return 0;

undo:
	/* DSA calls no bridge leave when the join itself fails. */
	rtl8372n_port_bridge_isolate(ds, port, &bridge);
	rtl8372n_port_stp_state_set(ds, port, dsa_to_port(ds, port)->stp_state);
	dev_err(priv->dev, "port %d: bridge join failed: %d\n", port, ret);
	return ret;
}

/*
 * Return the port to standalone forwarding: isolation first, then no
 * flooding towards it, no learning, its learned entries flushed, and
 * FORWARDING. After a normal leave DSA resets the flags and the state again,
 * but when DSA rolls back a join this driver accepted, nothing follows this
 * call. A void op: failures are logged and the next step still runs.
 */
static void rtl8372n_port_bridge_leave(struct dsa_switch *ds, int port,
				       struct dsa_bridge bridge)
{
	struct rtl837x_priv *priv = ds->priv;
	int ret;

	if (!dsa_is_user_port(ds, port))
		return;

	rtl8372n_port_bridge_isolate(ds, port, &bridge);
	ret = rtl8372n_port_set_flood(ds, port, RTL8372N_FLOOD_FLAGS, false);
	if (ret)
		dev_err(priv->dev, "port %d: flooding not cleared: %d\n",
			port, ret);
	ret = rtl8372n_port_set_learning(ds, port, false);
	if (ret)
		dev_err(priv->dev, "port %d: learning not disabled: %d\n",
			port, ret);
	rtl8372n_port_fast_age(ds, port);
	ret = rtl8372n_stp_set_state(priv, port, RTL8372N_STP_FORWARDING);
	if (ret)
		dev_err(priv->dev, "port %d: STP forwarding not restored: %d\n",
			port, ret);
	dev_dbg(priv->dev, "port %d left bridge %u\n", port, bridge.num);
}

/*
 * DSA expects standalone and software-bridged ports to be VLAN-unaware: the
 * switch must accept a frame whatever its VID and hand it to the CPU exactly as
 * received, so that 8021q uppers and the software bridge's VLAN filtering work.
 * As in rtl8365mb, the VLAN function stays enabled and VLAN awareness is a
 * per-port state. A VLAN-unaware port has no ingress membership filter, its
 * frames are exempt from the egress membership check on every available port,
 * and frames leave it with their tag format, VID and priority untouched
 * (REAL_KEEP). A VLAN-aware port is filtered on both sides and egresses frames
 * tagged or untagged as its VLAN entries say. Isolation, not VLAN membership,
 * keeps standalone ports apart.
 */
static int rtl8372n_port_set_vlan_filtering(struct dsa_switch *ds, int port,
					    bool filtering)
{
	struct rtl837x_priv *priv = ds->priv;
	struct dsa_port *dp;
	int ret;

	ret = rtl837x_reg_bits_write(priv, RTL837X_VLAN_INGRESS_FILTER,
				     BIT(port), filtering);
	if (ret)
		return ret;
	ret = rtl837x_reg_bits_write(priv, RTL837X_VLAN_INGRESS_CTRL,
				     RTL837X_VLAN_ACCEPT_FRAME_TYPE_MASK(port),
				     RTL837X_VLAN_ACCEPT_FRAME_TYPE_ALL);
	if (ret)
		return ret;
	dsa_switch_for_each_available_port(dp, ds) {
		u32 bit = RTL837X_VLAN_EGRESS_TRANS_BIT(dp->index, port);

		ret = regmap_update_bits(priv->map,
					 RTL837X_VLAN_EGRESS_TRANS_REG(dp->index),
					 bit, filtering ? 0 : bit);
		if (ret)
			return ret;
	}
	return rtl837x_reg_bits_write(priv, RTL837X_VLAN_EGRESS_TAG,
				      RTL837X_VLAN_EGRESS_TAG_MODE_MASK(port),
				      filtering ? RTL837X_VLAN_EGRESS_MODE_ORIGINAL :
						  RTL837X_VLAN_EGRESS_MODE_REAL_KEEP);
}

/*
 * Offloaded ports stay VLAN-unaware for now: bridge join refuses a
 * VLAN-aware bridge, and this refuses turning VLAN filtering on under an
 * offloaded one. It must be -EINVAL: DSA's join sync and the bridge's
 * runtime toggle both ignore -EOPNOTSUPP, which would leave a filtering
 * bridge on unfiltered hardware ports.
 */
static int rtl8372n_port_vlan_filtering(struct dsa_switch *ds, int port,
					bool vlan_filtering,
					struct netlink_ext_ack *extack)
{
	if (!vlan_filtering)
		return 0;

	NL_SET_ERR_MSG_MOD(extack,
			   "Cannot enable VLAN filtering on an offloaded bridge; create it VLAN-aware");
	return -EINVAL;
}

void rtl8372n_report_p0_reg(struct rtl837x_priv *priv, const char *name,
			    u32 reg, u32 mask, u32 expected)
{
	u32 value;
	int ret;

	ret = rtl837x_reg_read(priv, reg, &value);
	if (ret) {
		dev_warn(priv->dev, "P0 readback %s at %#x failed: %d\n",
			 name, reg, ret);
		return;
	}

	if ((value & mask) == (expected & mask))
		dev_info(priv->dev,
			 "P0 readback %s at %#x: value=%#x expected=%#x mask=%#x PASS\n",
			 name, reg, value, expected, mask);
	else
		dev_warn(priv->dev,
			 "P0 readback %s at %#x: value=%#x expected=%#x mask=%#x MISMATCH\n",
			 name, reg, value, expected, mask);
}

static void rtl8372n_report_p0_field(struct rtl837x_priv *priv,
				     const char *name, u32 reg, u32 mask,
				     u32 expected)
{
	u32 value;
	int ret;

	ret = rtl837x_reg_bits_read(priv, reg, mask, &value);
	if (ret) {
		dev_warn(priv->dev, "P0 readback %s at %#x failed: %d\n",
			 name, reg, ret);
		return;
	}

	if (value == expected)
		dev_info(priv->dev,
			 "P0 readback %s at %#x: value=%#x expected=%#x mask=%#x PASS\n",
			 name, reg, value, expected, mask);
	else
		dev_warn(priv->dev,
			 "P0 readback %s at %#x: value=%#x expected=%#x MISMATCH\n",
			 name, reg, value, expected);
}

static const u32 rtl8372n_cpu_flood_regs[] = {
	RTL837X_L2_UNKNOWN_UC_FLOOD, RTL837X_L2_UNKNOWN_MC_FLOOD,
	RTL837X_IPV4_UNKNOWN_MC_FLOOD, RTL837X_IPV6_UNKNOWN_MC_FLOOD,
	RTL837X_L2_BROADCAST_FLOOD,
};

static void rtl8372n_report_p0_readback(struct dsa_switch *ds, u16 members)
{
	struct rtl837x_priv *priv = ds->priv;
	struct rtl837x_vlan_entry vlan;
	struct dsa_port *dp;
	unsigned int cpu_mask = dsa_cpu_ports(ds);
	unsigned int user_mask = dsa_user_ports(ds);
	u32 accept_mask = 0, egress_tag_mask = 0, egress_tag = 0;
	u32 expected_vlan;
	int port, i, ret;

	/*
	 * VLAN 1 must still hold the canonical word setup wrote, after the
	 * VLAN control writes that followed it. Bits 31:26 are reserved.
	 */
	expected_vlan = rtl837x_vlan_entry_canonical(members, members);
	ret = rtl837x_vlan_entry_read(priv, 1, &vlan);
	if (ret) {
		dev_warn(priv->dev, "P0 readback VLAN 1 table entry failed: %d\n",
			 ret);
	} else if ((vlan.raw & RTL837X_VLAN_ENTRY_FIELDS) == expected_vlan) {
		dev_info(priv->dev,
			 "P0 readback VLAN 1 data0=%#x expected=%#x PASS\n",
			 vlan.raw, expected_vlan);
	} else {
		dev_warn(priv->dev,
			 "P0 readback VLAN 1 data0=%#x expected=%#x MISMATCH\n",
			 vlan.raw, expected_vlan);
	}

	for (port = 0; port <= RTL8372N_INTERNAL_CPU_PORT; port++) {
		u32 isolation = 0;

		if (user_mask & BIT(port))
			isolation = cpu_mask;
		else if (cpu_mask & BIT(port))
			isolation = user_mask;
		rtl8372n_report_p0_reg(priv, "port isolation",
				       RTL837X_PORT_ISOLATION_REG(port),
				       GENMASK(9, 0), isolation);
		rtl8372n_report_p0_field(priv, "learning limit",
					 RTL837X_L2_LEARN_LIMIT_REG(port),
					 RTL837X_L2_LEARN_LIMIT_MASK, 0);
	}

	for (i = 0; i < ARRAY_SIZE(rtl8372n_cpu_flood_regs); i++)
		rtl8372n_report_p0_reg(priv, "CPU flood destination",
				       rtl8372n_cpu_flood_regs[i],
				       GENMASK(9, 0), cpu_mask);

	dsa_switch_for_each_available_port(dp, ds)
		rtl8372n_report_p0_field(priv, "PVID",
					 RTL837X_PORT_PVID_REG(dp->index),
					 RTL837X_PORT_PVID_MASK(dp->index), 1);

	dsa_switch_for_each_available_port(dp, ds) {
		accept_mask |= RTL837X_VLAN_ACCEPT_FRAME_TYPE_MASK(dp->index);
		egress_tag_mask |= RTL837X_VLAN_EGRESS_TAG_MODE_MASK(dp->index);
		egress_tag |= RTL837X_VLAN_EGRESS_MODE_REAL_KEEP <<
			      __ffs(RTL837X_VLAN_EGRESS_TAG_MODE_MASK(dp->index));
		rtl8372n_report_p0_field(priv, "VLAN egress transparent ports",
					 RTL837X_VLAN_EGRESS_TRANS_REG(dp->index),
					 RTL837X_VLAN_EGRESS_TRANS_MASK(dp->index),
					 members);
	}
	rtl8372n_report_p0_reg(priv, "VLAN accepted frame types",
			       RTL837X_VLAN_INGRESS_CTRL, accept_mask, 0);
	rtl8372n_report_p0_reg(priv, "VLAN ingress filter",
			       RTL837X_VLAN_INGRESS_FILTER, members, 0);
	rtl8372n_report_p0_reg(priv, "VLAN egress tag mode",
			       RTL837X_VLAN_EGRESS_TAG, egress_tag_mask, egress_tag);
	rtl8372n_report_p0_field(priv, "VLAN filter enable",
				 RTL837X_VLAN_CTRL, RTL837X_VLAN_CTRL_FILTER, 1);
	rtl8372n_l2_report_readback(ds);
}

static int rtl8372n_configure_sds_polarity(struct rtl837x_priv *priv)
{
	static const char * const rx_swap[] = {
		"sds0-rx-swap", "sds1-rx-swap",
	};
	static const char * const tx_swap[] = {
		"sds0-tx-swap", "sds1-tx-swap",
	};
	struct device_node *np = priv->dev->of_node;
	int sds, ret;

	for (sds = 0; sds < ARRAY_SIZE(rx_swap); sds++) {
		if (of_property_read_bool(np, rx_swap[sds])) {
			ret = rtl837x_sds_reg_bits_write(priv, sds,
						 RTL837X_SDS_PAGE_XSG,
						 RTL837X_SDS_XSG_REG_POLARITY,
						 RTL837X_SDS_XSG_RX_POLARITY_SWAP,
						 true);
			if (ret)
				return ret;

			ret = rtl837x_sds_reg_bits_write(priv, sds,
						 RTL837X_SDS_PAGE_10GBASE_R,
						 RTL837X_SDS_10G_REG_POLARITY,
						 RTL837X_SDS_10G_RX_POLARITY_SWAP,
						 true);
			if (ret)
				return ret;
		}

		if (of_property_read_bool(np, tx_swap[sds])) {
			ret = rtl837x_sds_reg_bits_write(priv, sds,
						 RTL837X_SDS_PAGE_XSG,
						 RTL837X_SDS_XSG_REG_POLARITY,
						 RTL837X_SDS_XSG_TX_POLARITY_SWAP,
						 true);
			if (ret)
				return ret;

			ret = rtl837x_sds_reg_bits_write(priv, sds,
						 RTL837X_SDS_PAGE_10GBASE_R,
						 RTL837X_SDS_10G_REG_POLARITY,
						 RTL837X_SDS_10G_TX_POLARITY_SWAP,
						 true);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int rtl8372n_validate_topology(struct dsa_switch *ds, u16 *members)
{
	struct rtl837x_priv *priv = ds->priv;
	struct dsa_port *dp;
	phy_interface_t interface;
	unsigned int cpu_count = 0;
	int ret;

	*members = 0;
	dsa_switch_for_each_available_port(dp, ds) {
		if (dsa_port_is_dsa(dp))
			return -EOPNOTSUPP;
		ret = of_get_phy_mode(dp->dn, &interface);
		if (ret)
			return dev_err_probe(priv->dev, ret,
					     "port %d needs phy-mode\n", dp->index);
		if (dsa_port_is_cpu(dp)) {
			if (!IS_SERDES_PORT(dp->index) ||
			    interface != PHY_INTERFACE_MODE_10GBASER ||
			    !of_phy_is_fixed_link(dp->dn))
				return -EOPNOTSUPP;
			cpu_count++;
		} else if (dsa_port_is_user(dp)) {
			if (dp->index < 4 || dp->index > 7 ||
			    interface != PHY_INTERFACE_MODE_INTERNAL)
				return -EOPNOTSUPP;
		} else {
			return -EOPNOTSUPP;
		}
		*members |= BIT(dp->index);
	}
	return cpu_count == 1 && dsa_user_ports(ds) ? 0 : -EOPNOTSUPP;
}

static void rtl8372n_quiesce(struct dsa_switch *ds)
{
	struct rtl837x_priv *priv = ds->priv;
	int port, ret;

	/* Best effort on setup failure/teardown; never hide the first error. */
	for (port = 0; port <= RTL8372N_INTERNAL_CPU_PORT; port++) {
		ret = rtl8372n_port_set_isolation(priv, port, 0);
		if (ret)
			dev_warn(priv->dev, "failed to isolate port %d: %d\n", port, ret);
	}
	ret = rtl837x_phys_write_c45(priv, RTL8372N_PHY_PORT_MASK,
				    31, 0xa610, 0x2858);
	if (ret)
		dev_warn(priv->dev, "failed to power down PHYs: %d\n", ret);
}

static int rtl8372n_setup_cpu_forwarding(struct dsa_switch *ds)
{
	struct rtl837x_priv *priv = ds->priv;
	unsigned int cpu_mask = dsa_cpu_ports(ds);
	unsigned int user_mask = dsa_user_ports(ds);
	int port, i, ret;

	/* Every user port starts standalone: it reaches only the CPU, learns
	 * nothing and receives no flooded frames. Only bridge join opens a
	 * user-to-user path. The internal 8051 port 9 reaches no port and
	 * learns nothing.
	 */
	for (port = 0; port <= RTL8372N_INTERNAL_CPU_PORT; port++) {
		u32 mask = 0;

		if (user_mask & BIT(port))
			mask = cpu_mask;
		else if (cpu_mask & BIT(port))
			mask = user_mask;
		ret = rtl8372n_port_set_isolation(priv, port, mask);
		if (ret)
			return ret;
		ret = rtl837x_reg_bits_write(priv, RTL837X_L2_LEARN_LIMIT_REG(port),
					     RTL837X_L2_LEARN_LIMIT_MASK, 0);
		if (ret)
			return ret;
	}
	for (i = 0; i < ARRAY_SIZE(rtl8372n_cpu_flood_regs); i++) {
		ret = regmap_update_bits(priv->map, rtl8372n_cpu_flood_regs[i],
					 RTL837X_L2_FLOOD_MASK, cpu_mask);
		if (ret)
			return ret;
	}
	return 0;
}

static int rtl8372n_setup(struct dsa_switch *ds)
{
	struct rtl837x_priv *priv = ds->priv;
	struct rtl8372n *chip_data = priv->chip_data;
	struct dsa_port *dp;
	u16 members;
	int port, ret;

	ret = rtl8372n_validate_topology(ds, &members);
	if (ret)
		return dev_err_probe(priv->dev, ret, "unsupported P0 topology\n");
	/*
	 * The L2 table has no per-bridge database, so one bridge is offloaded
	 * and a second stays in software with CPU-only ports. DSA installs no
	 * learned host addresses on the CPU port (no assisted learning).
	 */
	ds->max_num_bridges = 1;
	ds->fdb_isolation = false;
	ds->assisted_learning_on_cpu_port = false;
	ret = rtl8372n_soft_reset_chip(priv);
	if (ret)
		return dev_err_probe(priv->dev, ret, "failed to reset switch\n");

	for (port = 3; port <= 8; port += 5) {
		chip_data->pcs[port].pcs.ops = &rtl8372n_sds_pcs_ops;
		__set_bit(PHY_INTERFACE_MODE_10GBASER,
			  chip_data->pcs[port].pcs.supported_interfaces);
		chip_data->pcs[port].priv = priv;
		chip_data->pcs[port].index = port;
	}

	ret = rtl837x_reg_bits_write(priv, RTL837X_SMI_MAC_TYPE,
				     RTL837X_SMI_MAC_PORT3_TYPE, 0);
	if (ret)
		goto fail;
	ret = rtl837x_reg_bits_write(priv, RTL837X_SMI_MAC_TYPE,
				     RTL837X_SMI_MAC_PORT8_TYPE, 0);
	if (ret)
		goto fail;
	ret = rtl837x_reg_bits_write(priv, RTL837X_SMI_PORT_POLLING,
				     RTL837X_SMI_PORT_POLLING_4_7, 0xf);
	if (ret)
		goto fail;
	ret = rtl837x_reg_bits_write(priv, RTL837X_SMI_CTRL,
				     RTL837X_SMI_MDC_ENABLE, 0x7);
	if (ret)
		goto fail;
	ret = rtl837x_reg_bits_write(priv, RTL837X_SMI_GLOBAL_CTRL,
				     RTL837X_SMI_POLLING_MASK, 0x1f8);
	if (ret)
		goto fail;
	msleep(20);

	ret = rtl8372n_configure_sds_polarity(priv);
	if (ret)
		goto fail;
	ret = rtl837x_phys_write_c45(priv, RTL8372N_PHY_PORT_MASK,
				    31, 0xa610, 0x2858);
	if (ret)
		goto fail;
	ret = rtl837x_vlan_entry_write(priv, 1, members, members);
	if (ret)
		goto fail;
	dsa_switch_for_each_available_port(dp, ds) {
		ret = rtl837x_reg_bits_write(priv, RTL837X_PORT_PVID_REG(dp->index),
					     RTL837X_PORT_PVID_MASK(dp->index), 1);
		if (ret)
			goto fail;
	}
	ret = rtl8372n_setup_cpu_forwarding(ds);
	if (ret)
		goto fail;
	ret = rtl8372n_l2_setup(ds);
	if (ret)
		goto fail;
	/*
	 * Start from an empty L2 table whatever the reset left. Learning is
	 * off on every port from here on, so nothing is learned again before
	 * DSA brings the ports up.
	 */
	ret = rtl837x_l2_clear(priv);
	if (ret)
		goto fail;
	/*
	 * Make every port VLAN-unaware before the VLAN function is enabled.
	 * Clear the egress transparency fields first so that they hold
	 * exactly the available ports afterwards, whatever their reset value.
	 */
	dsa_switch_for_each_available_port(dp, ds) {
		ret = rtl837x_reg_bits_write(priv,
					     RTL837X_VLAN_EGRESS_TRANS_REG(dp->index),
					     RTL837X_VLAN_EGRESS_TRANS_MASK(dp->index),
					     0);
		if (ret)
			goto fail;
	}
	dsa_switch_for_each_available_port(dp, ds) {
		ret = rtl8372n_port_set_vlan_filtering(ds, dp->index, false);
		if (ret)
			goto fail;
	}
	ret = rtl837x_reg_bits_write(priv, RTL837X_VLAN_CTRL,
				     RTL837X_VLAN_CTRL_FILTER, 1);
	if (ret)
		goto fail;
	ret = rtl8372n_set_tag_rtl(ds);
	if (ret)
		goto fail;
	/* From here on the switch never forwards link-local frames. */
	ret = rtl8372n_trap_setup(ds);
	if (ret)
		goto fail;
	rtl8372n_report_p0_readback(ds, members);
	/*
	 * Register PHYs only after forwarding is set up. They stay in PMA low
	 * power (OCP 0xa610 mirrors PMA/PMD control 1) until phylib resumes
	 * them on attach; from then on phylib owns their power state.
	 */
	ret = rtl8372n_setup_mdio(priv);
	if (ret)
		goto fail;
	return 0;

fail:
	rtl8372n_quiesce(ds);
	return dev_err_probe(priv->dev, ret, "switch setup failed\n");
}

static const struct dsa_switch_ops rtl8372n_switch_ops_mdio = {
	.get_tag_protocol = rtl8372n_get_tag_protocol,
	.setup = rtl8372n_setup,
	.teardown = rtl8372n_quiesce,
	.phylink_get_caps = rtl8372n_phylink_get_caps,
	.set_ageing_time = rtl8372n_set_ageing_time,
	.port_bridge_join = rtl8372n_port_bridge_join,
	.port_bridge_leave = rtl8372n_port_bridge_leave,
	.port_pre_bridge_flags = rtl8372n_port_pre_bridge_flags,
	.port_bridge_flags = rtl8372n_port_bridge_flags,
	.port_vlan_filtering = rtl8372n_port_vlan_filtering,
	.port_stp_state_set = rtl8372n_port_stp_state_set,
	.port_fast_age = rtl8372n_port_fast_age,
	.port_fdb_add = rtl8372n_port_fdb_add,
	.port_fdb_del = rtl8372n_port_fdb_del,
	.port_fdb_dump = rtl8372n_port_fdb_dump,
};

static const struct rtl837x_ops rtl8372n_ops = {
	.detect		= rtl8372n_detect,

	.phy_read_c45   = rtl837x_phy_read_c45,
	.phy_write_c45  = rtl837x_phy_write_c45,
};

const struct rtl837x_variant rtl8372n_variant = {
	.ds_ops_mdio = &rtl8372n_switch_ops_mdio,
	.ops = &rtl8372n_ops,
	.pl_mac_ops = &rtl8372n_phylink_mac_ops,
	.chip_data_sz = sizeof(struct rtl8372n),
};
