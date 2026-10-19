/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2025 StarField Xu <air_jinkela@163.com> */
#ifndef __RTL837X_H__
#define __RTL837X_H__

#include <linux/bitfield.h>
#include <linux/gpio/consumer.h>
#include <linux/if_bridge.h>
#include <linux/if_ether.h>
#include <linux/mutex.h>
#include <linux/of_mdio.h>
#include <linux/regmap.h>
#include <net/dsa.h>

#include "rtl837x_regmap.h"

#define MDC_MDIO_CTRL_REG   RTL837X_SMI_MDIO_CTRL_REG
#define MDC_MDIO_ADDR_REG   RTL837X_SMI_MDIO_ADDR_REG
#define MDC_MDIO_DATA_LOW   RTL837X_SMI_MDIO_DATA_LOW
#define MDC_MDIO_DATA_HIGH  RTL837X_SMI_MDIO_DATA_HIGH
#define MDC_MDIO_READ_CMD   RTL837X_SMI_MDIO_READ_CMD
#define MDC_MDIO_WRITE_CMD  RTL837X_SMI_MDIO_WRITE_CMD

#define RTL8372N_NUM_PORTS 9
#define RTL8372N_PHY_PORT_MASK GENMASK(7, 4)
/* Hardware port 9 is the internal 8051 CPU; it is not a DSA port. */
#define RTL8372N_INTERNAL_CPU_PORT 9
/* VLAN that VLAN-unaware ports classify untagged frames into (PVID). */
#define RTL8372N_UNAWARE_VID 1
/* Bridge port flags the switch offloads; the bridge refuses the others. */
#define RTL8372N_FLOOD_FLAGS (BR_FLOOD | BR_MCAST_FLOOD | BR_BCAST_FLOOD)
#define RTL8372N_BRIDGE_FLAGS (BR_LEARNING | RTL8372N_FLOOD_FLAGS)

/* CIST port state encoding (MNeroba/openwrt-flint3#2, bench-verified). */
enum rtl8372n_stp_state {
	RTL8372N_STP_DISABLED,
	RTL8372N_STP_BLOCKING,
	RTL8372N_STP_LEARNING,
	RTL8372N_STP_FORWARDING,
};

struct rtl837x_ops;
struct rtl837x_priv {
	struct device *dev;
	struct gpio_desc *reset;
	struct mii_bus *bus;
	struct regmap *map;
	struct mutex map_lock;
	struct mutex sds_lock;
	/* Child MII bus -> PHY/SDS engine -> regmap -> parent MII bus. */
	struct mutex phy_lock;
	/*
	 * Serialises indirect table (VLAN/L2) transactions and the L2 flush
	 * engines. Lock order: table_lock -> map_lock -> parent MII bus.
	 */
	struct mutex table_lock;
	int mdio_addr;
	unsigned int num_ports;
	struct dsa_switch *ds;
	const struct rtl837x_ops *ops;
	int (*write_reset)(struct rtl837x_priv *priv);
	void *chip_data;
};

struct rtl837x_variant {
	const struct dsa_switch_ops *ds_ops_mdio;
	const struct rtl837x_ops *ops;
	const struct phylink_mac_ops *pl_mac_ops;
	size_t chip_data_sz;
};

struct rtl837x_ops {
	int (*detect)(struct rtl837x_priv *priv);
	int (*phy_read_c45)(struct rtl837x_priv *priv, int phy, int devad,
			    int regnum, u16 *pval);
	int (*phy_write_c45)(struct rtl837x_priv *priv, int phy, int devad,
			     int regnum, u16 val);
};

#define rtl837x_reg_read(priv, reg, pval) regmap_read((priv)->map, (reg), (pval))
#define rtl837x_reg_write(priv, reg, val) regmap_write((priv)->map, (reg), (val))

int rtl837x_reg_bits_read(struct rtl837x_priv *priv, u32 reg, u32 mask,
			  u32 *pval);
int rtl837x_reg_bits_write(struct rtl837x_priv *priv, u32 reg, u32 mask,
			   u32 val);
int rtl837x_sds_reg_bits_write(struct rtl837x_priv *priv, u8 sds, u8 page,
			       u8 reg, u16 mask, bool set);
int rtl837x_phy_read_c45(struct rtl837x_priv *priv, int phy, int devad,
			 int regnum, u16 *pval);
int rtl837x_phy_write_c45(struct rtl837x_priv *priv, int phy, int devad,
			  int regnum, u16 val);
int rtl837x_phys_write_c45(struct rtl837x_priv *priv, u16 phy_mask,
			   int devad, int regnum, u16 val);

/* Decoded VLAN table entry; @raw keeps the whole word for logging. */
struct rtl837x_vlan_entry {
	u32 raw;
	u16 member;
	u16 untag;
	u8 fid_msti;
	bool svlan_chk;
	bool ivl;
};

/* L2 unicast entry. @vid is a VID when @ivl is set, a FID otherwise. */
struct rtl837x_l2_uc_entry {
	u8 mac[ETH_ALEN];
	u16 vid;
	bool ivl;
	u8 port;
	u8 age;
	bool auth;
	bool is_static;
};

/*
 * The only VLAN entry format the driver writes: IVL, FID/MSTI 0 (so the CIST
 * state register governs every VLAN), SVLAN check and reserved bits 0.
 */
static inline u32 rtl837x_vlan_entry_canonical(u16 member, u16 untag)
{
	return FIELD_PREP(RTL837X_VLAN_ENTRY_MEMBER, member) |
	       FIELD_PREP(RTL837X_VLAN_ENTRY_UNTAG, untag) |
	       FIELD_PREP(RTL837X_VLAN_ENTRY_FID_MSTI, 0) |
	       RTL837X_VLAN_ENTRY_IVL;
}

int rtl837x_vlan_entry_write(struct rtl837x_priv *priv, u16 vid, u16 member,
			     u16 untag);
int rtl837x_vlan_entry_read(struct rtl837x_priv *priv, u16 vid,
			    struct rtl837x_vlan_entry *entry);
int rtl837x_l2_lookup(struct rtl837x_priv *priv, const u8 *mac, u16 vid,
		      struct rtl837x_l2_uc_entry *entry);
int rtl837x_l2_add_static(struct rtl837x_priv *priv, const u8 *mac, u16 vid,
			  int port);
int rtl837x_l2_del_static(struct rtl837x_priv *priv, const u8 *mac, u16 vid,
			  int port);
int rtl837x_l2_next_uc(struct rtl837x_priv *priv, u16 *index, int port,
		       struct rtl837x_l2_uc_entry *entry);
int rtl837x_l2_clear(struct rtl837x_priv *priv);
int rtl837x_l2_flush_port(struct rtl837x_priv *priv, int port);

int rtl8372n_stp_set_state(struct rtl837x_priv *priv, int port,
			   enum rtl8372n_stp_state state);
int rtl8372n_port_set_learning(struct dsa_switch *ds, int port, bool enable);
int rtl8372n_port_set_flood(struct dsa_switch *ds, int port,
			    unsigned long flags, bool enable);
void rtl8372n_port_stp_state_set(struct dsa_switch *ds, int port, u8 state);
int rtl8372n_port_pre_bridge_flags(struct dsa_switch *ds, int port,
				   struct switchdev_brport_flags flags,
				   struct netlink_ext_ack *extack);
int rtl8372n_port_bridge_flags(struct dsa_switch *ds, int port,
			       struct switchdev_brport_flags flags,
			       struct netlink_ext_ack *extack);
void rtl8372n_port_fast_age(struct dsa_switch *ds, int port);
int rtl8372n_set_ageing_time(struct dsa_switch *ds, unsigned int msecs);
int rtl8372n_port_fdb_add(struct dsa_switch *ds, int port,
			  const unsigned char *addr, u16 vid,
			  struct dsa_db db);
int rtl8372n_port_fdb_del(struct dsa_switch *ds, int port,
			  const unsigned char *addr, u16 vid,
			  struct dsa_db db);
int rtl8372n_port_fdb_dump(struct dsa_switch *ds, int port,
			   dsa_fdb_dump_cb_t *cb, void *data);
int rtl8372n_l2_setup(struct dsa_switch *ds);
int rtl8372n_trap_setup(struct dsa_switch *ds);
void rtl8372n_l2_report_readback(struct dsa_switch *ds);
void rtl8372n_report_p0_reg(struct rtl837x_priv *priv, const char *name,
			    u32 reg, u32 mask, u32 expected);

extern const struct rtl837x_variant rtl8372n_variant;

#endif /* __RTL837X_H__ */
