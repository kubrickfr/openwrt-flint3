# Linux 6.18.52 and wireless backports 7.2 upgrade

This upgrade starts at `cc998e41f6` and targets the GL-BE9300 on
`qualcommbe/ipq53xx`. Other targets are not build-tested. Hardware validation
is still required before recommending these images for use.

## Sources

- Linux 6.18.52: OpenWrt kernel bump commits from `7105bec48f` through
  `ddbf9c8557`, restricted to the generic and qualcommbe 6.18 files.
- mac80211 7.2, release 3: OpenWrt `7f347425eb570b9e7f7045873260a5c24fc5cddc`.
- Existing firmware peer-ID limit, association-status and quarantine fixes:
  refreshed against the 7.2 versions in JiaY-shi `42a7889a41`, retaining the
  local fixes' behavior with 7.2's encoded peer IDs and common free helper.
- Firmware blobs, hostapd and the PPE hardware flow-offload feature series
  are not upgraded by this change.

## Patch review

The complete generic backport, pending and hack series and qualcommbe series
are applied in build order with zero fuzz. Offset-only applications are
refreshed and checked again from pristine sources. The entire mac80211
package patch stack is checked the same way, including drivers not selected
in the Flint 3 build. Refreshes preserve patch descriptions and authorship.

Specific reconciliations:

- phylink internal PCS handling: preserve Linux stable's `IS_ERR_OR_NULL()`
  check when selecting a PCS through the MAC callback. Do not restore the
  old NULL-only check.
- Realtek rtl8365mb FDB backport: refresh context for stable's sleeping GPIO
  reset accessor. No changes to the separate RTL837x driver.
- Qualcomm SCM PAS metadata-size support: use stable's renamed `pas_id`
  argument in both old and new secure-call paths.
- Remove local CMN PLL divider and BAM command-mask patches: the complete
  fixes are already present in 6.18.52.
- Remove the rc4 AES-CMAC/S2V reversions: backports 7.2 supplies the AES-CMAC
  compatibility implementation. Remove the now-unnecessary explicit
  mac80211 dependency on the kernel crypto-CMAC module.
- Remove the local QMI `kernel_connect()` cast reversion: backports 7.2
  now supplies the `sockaddr_unsized` compatibility wrapper for 6.18.
- Remove the AHB tasklet conversion together with upstream's removal of
  the PCI tasklet reversion: both buses must use the common CE workqueue
  member in 7.2.
- Keep the legacy IPQ5332 BDF/firmware memory overrides. Refresh the BDF
  default-offset patch for the expanded hardware parameters structure.
- Preserve nonzero firmware-limited MLO peer-ID allocation. Use encoded IDs
  in station state, preserve the pending-ID constant/completion, and retain
  bounded, masked ID release and quarantine across error/recovery paths.
  The old separate SSR mask patch is superseded by the checked common free
  helper and must not be applied a second time.
- Keep PPE patches 0375–0379 and the TX queue wake fixes.
- Refresh iwinfo's multi-radio patch by one line; no functional changes or
  source-version bump. Its pristine replay has no offsets or fuzz.
- Refresh 11 pre-existing hostapd patch offsets without changing its version.
  In the ucode interface-removal patch, preserve the earlier MLD fix's
  `hostapd_remove_hapd_iface()` helper in the context around the PHY-name match.
- Import OpenWrt `b837748095`: declare `kmod-nft-core` as a dependency of
  `kmod-nf-flow`, matching the flowtable path code's references to `nf_tables`.
  This was confirmed by the module dependency check during packaging.

## Reproducing the strict application checks

In a clean build directory, use:

```sh
make target/linux/prepare PATCH="$PWD/scripts/patch-strict.sh" V=s
make package/kernel/mac80211/prepare PATCH="$PWD/scripts/patch-strict.sh" V=s
```

Preparation stamps can skip these commands on an already prepared tree;
remove the relevant build output or use a clean worktree to repeat the test.
The wrapper treats any offset or fuzz as an error, not just rejected hunks.

## Validation

Completed checks:

- Linux source SHA-256 matches OpenWrt's published 6.18.52 value.
- Backports source SHA-256 matches OpenWrt's published 7.2 value.
- Kernel build-system preparation: 504 patches, zero offsets, zero fuzz.
- All 552 patched kernel files match the independent audit tree byte-for-byte.
- Final pristine wireless replay: 183 patches, zero offsets, zero fuzz.
- All 193 patched wireless files match the independent audit tree byte-for-byte.
- Final pristine hostapd replay: 70 patches, zero offsets, zero fuzz.
- Strict wrapper accepts exact patches and rejects offset-only and fuzzy cases.
- `git diff --check` passes.
- `make target/linux/compile -j8`: PASS, including in-tree modules and
  Qualcomm PPE/EDMA, SCM and WCSS remoteproc. Modpost emits missing
  `MODULE_DESCRIPTION()` metadata warnings; no compilation/link errors.

- mac80211/cfg80211/ath12k (including `ath12k_wifi7`) compile and package: PASS.

- RTL837x DSA switch driver compile and package: PASS, without API changes.
- Both ath12k modules and RTL837x report `vermagic=6.18.52`.
- Toolchain Linux 6.18.52 headers and musl rebuild: PASS.

- Full `make -j8 V=s PATCH="$PWD/scripts/patch-strict.sh"`: PASS.
  Factory, sysupgrade and initramfs images were produced. The successful final
  build log contains no offsets, fuzz, rejected patches or build errors.
- All artifact SHA-256 checksums verify. Inspection of the actual sysupgrade
  SquashFS confirms only `/lib/modules/6.18.52`, including both ath12k modules,
  cfg80211/mac80211, RTL837x, PPE and netfilter modules.

Detailed logs and the selected package configuration (`build.config`) are in
`logs/upgrade/` in this worktree.

The build uses the original checkout's selected GL-BE9300 package configuration
and copies of its host tools/toolchain. Feeds are held at their existing revisions:
packages `854113b40e4159536124e60e3b6cec93e4c7461e`,
LuCI `d2705839472b7a2cb70fbc26d3762ed5a976cc36`.
Unselected local feed-install links for librespeed and squeezelite are omitted
from this validation workspace to avoid pre-existing recursive Kconfig metadata;
no tracked feed source or selected package is removed.

Hardware checks still required: cold boot, eMMC/sysupgrade, LAN/WAN throughput
and FIFO/drop counters, bridge VLANs, USB3, tri-band Wi-Fi/MLO, reconnects and
firmware recovery under sustained load. No router has been flashed.

## Outputs and scope

The upgrade was developed in branch `codex/kernel-6.18.52`, in
`.worktrees/kernel-6.18.52`. The original checkout's uncommitted kernel config
and fan/device-tree edits were not copied or changed. Build version metadata
names the base commit `cc998e41f6` because the images were built before the
upgrade was committed; these artifacts include the upgrade changes.

Artifacts are under `bin/targets/qualcommbe/ipq53xx/`:

- `openwrt-qualcommbe-ipq53xx-glinet_gl-be9300-squashfs-sysupgrade.bin`
- `openwrt-qualcommbe-ipq53xx-glinet_gl-be9300-squashfs-factory.bin`
- `openwrt-qualcommbe-ipq53xx-glinet_gl-be9300-initramfs-uImage.itb`
- Package manifest, build configuration, feed revisions and `sha256sums`.

Sysupgrade SHA-256:
`46229c871e62a29d12419bf2c6b0c1472d53ec171fb43b74df75b526f7ff77a8`.

The shared generic 6.18 version file affects other 6.18 targets too, but their
platform-specific patch sets were not refreshed or build-tested. This worktree
validates GL-BE9300/qualcommbe only.
