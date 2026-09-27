# RTL837x DSA switch driver

This OpenWrt kernel package provides a Linux DSA driver for Realtek RTL837x
switch chips.

The driver was refactored from the swconfig/GSW driver at:

https://github.com/RuijieNetworksCommunity/rtl837x-gsw-driver.git

The original switch setup, Realtek SDK integration, MDIO/regmap access, GPIO
support and chip initialization code were kept where practical. The OpenWrt
integration and switch control path have been reworked to use the modern Linux
DSA model instead of swconfig.

## Package

The package name is:

```text
kmod-rtl837x-dsa
```

It builds the switch driver module:

```text
rtl837x_dsa.ko
```

The driver uses the chip's own RTL8_4 CPU tag (EtherType 0x8899, `tag_rtl8_4.ko`)
by default: the source/destination port is an explicit field, so per-port
identity stays precise under a bridge in either VLAN mode. The IPQ5332 PPE
parser cannot classify past that 8-byte header on its own, so the PPE
driver teaches it to treat the tag as an S+C VLAN pair whenever an EDMA port
is the conduit of an rtl8_4 switch (see `edma_port.c` in the qualcommbe
patches); with that, hardware flow offload measures the same as with the
802.1Q-based tagger.

The 802.1Q-based tagger (`tag_vsc73xx_8021q.ko`, the upstream DSA
implementation of the `VSC73XX_8021Q` tag protocol, reused as this driver's
`tag_8021q` transport) is still built and remains selectable at runtime as
the fallback: with the conduit and every user port down,
`echo vsc73xx-8021q > /sys/class/net/<conduit>/dsa/tagging`. Under it, port
identity is lost once a port is bridged (the bridge takes the VLAN field the
port number is encoded in).

## Device Tree

Use a DSA `ports` description. The CPU port must reference the SoC Ethernet MAC
through the `ethernet` property, and user ports should be described as normal
DSA user ports.

Example:

```dts
&mdio {
	rtl837x: rtl837x-dsa@29 {
		compatible = "realtek,rtl837x";
		reg = <29>;

		#address-cells = <1>;
		#size-cells = <0>;

		reset-gpios = <&tlmm 42 GPIO_ACTIVE_HIGH>; /* optional */

		rtl837x,sds0mode = "10g-kr";

		sds0-rx-swap; /* optional */
		sds0-tx-swap; /* optional */
		sds1-rx-swap; /* optional */
		sds1-tx-swap; /* optional */
		phy-mdi-reverse; /* optional */
		phy-tx-polarity-swap; /* optional */

		gpio-controller; /* optional */
		#gpio-cells = <2>; /* optional */

		ports {
			#address-cells = <1>;
			#size-cells = <0>;

			port@3 {
				reg = <3>;
				label = "cpu";
				ethernet = <&gmac2>;
				phy-mode = "10gbase-r";

				fixed-link {
					speed = <10000>;
					full-duplex;
				};
			};

			port@4 {
				reg = <4>;
				label = "lan1";
				phy-mode = "internal";
			};

			port@5 {
				reg = <5>;
				label = "lan2";
				phy-mode = "internal";
			};

			port@6 {
				reg = <6>;
				label = "lan3";
				phy-mode = "internal";
			};

			port@7 {
				reg = <7>;
				label = "lan4";
				phy-mode = "internal";
			};
		};
	};
};
```

Legacy swconfig properties such as `rtl837x,cpu-port` and top-level `ethernet`
are only kept as compatibility fallback. New boards should use the DSA `ports`
binding.

## Network Configuration

DSA exposes switch ports as normal Linux netdevs, for example:

```text
lan1 lan2 lan3 lan4
```

OpenWrt board network setup should add these DSA user ports directly to
`br-lan`. Do not configure this driver through swconfig or `switch_vlan`.

## Notes

- RTL837x internal PHY status is handled by a small driver-specific PHY driver.
- Hardware MIB counters are exported through ethtool stats.
- LAN-to-LAN forwarding is expected to be handled by the switch hardware after
  ports join the same bridge.
