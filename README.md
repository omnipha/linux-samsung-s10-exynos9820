# Linux for Samsung Exynos 9820

An experimental Linux kernel tree for the Exynos Galaxy S10 family, based on **Linux 7.2-rc2**. This is kernel source, not a complete operating system or an installation image. The Exynos 9820 changes are not yet upstream Linux support.

Hardware results below come from **one Galaxy S10+ SM-G975F (`beyond2lte`)**, using a postmarketOS-based system and a custom boot setup. They were measured on development kernels; they do not prove that the current `main` commit or another phone has been boot-tested.

## Devices

| Device | Exynos model | Codename | Status |
|---|---|---|---|
| Galaxy S10e | SM-G970F | `beyond0lte` | Not supported by this tree yet |
| Galaxy S10 | SM-G973F | `beyond1lte` | Device tree present; not tested on hardware |
| Galaxy S10+ | SM-G975F | `beyond2lte` | Boots with a custom setup; partial support |
| Galaxy S10 5G | SM-G977B | `beyondx` | Not supported by this tree yet |

Snapdragon models and the Galaxy S10 Lite use different hardware and are not supported by this tree.

## S10+ hardware status

| Component | Observed on the test device |
|---|---|
| Boot and storage | Linux boots and mounts the root filesystem. Earlier ext4 corruption occurred; long-term storage reliability is not established. |
| Display and touch | Framebuffer console and Phosh display work; touch input was confirmed. Native display control, brightness and suspend remain unverified. |
| Wi-Fi | BCM4375 over PCIe: scan, WPA2, DHCP and SSH worked with device-specific firmware and calibration data. |
| Bluetooth | The controller powered on and discovered nearby devices. Pairing, audio and suspend remain unverified. |
| GPU | Mali-G76 with Panfrost rendered `kmscube`; thermal throttling and suspend remain unverified. |
| USB | Gadget networking and SSH worked; automatic data routing was verified on warm boots. USB host/OTG and power-role behavior remain unverified. |
| Battery and vibration | Battery and charger drivers bound, and a vibration test worked. Charging and haptic behavior are not fully characterized. |
| Audio | Speaker and microphone cards registered; capture tests passed. Audible playback and call audio remain unverified. |
| Cameras, mobile network, sensors, GPS and NFC | Not yet verified on this device. Camera register tables are present in the source tree; that is not proof of capture. |

The Wi-Fi and Bluetooth tests required device-specific firmware or calibration files not supplied by this repository. Results can differ across hardware revisions. See [hardware status](Documentation/beyond2lte/STATUS.md) and [bring-up evidence](Documentation/beyond2lte/bringup-evidence.md) for exact configurations, measurements and limitations.

## Build

The documented cross-build uses Clang/LLVM 17, LLD, flex, bison, bc, pahole, development headers for OpenSSL and libelf, and kmod/depmod.

```sh
git clone https://github.com/omnipha/linux-samsung-s10-exynos9820.git
cd linux-samsung-s10-exynos9820
make O=out ARCH=arm64 LLVM=-17 beyond2lte_defconfig
make -j"$(nproc)" O=out ARCH=arm64 LLVM=-17 Image modules dtbs
```

The outputs include `out/arch/arm64/boot/Image` and `out/arch/arm64/boot/dts/exynos/exynos9820-beyond2lte.dtb`. The `beyond1lte` device tree is also built but has not been tested on that device. Building is not a boot or hardware test.

Build on a computer or on the phone itself. [Build, download and deployment instructions](Documentation/beyond2lte/build-and-release.md) cover both paths and publishing manually; [downloadable kernel builds](https://github.com/omnipha/linux-samsung-s10-exynos9820/releases) are under Releases. Check each release's device-test status: an Image, device tree and modules are not a complete installer.

There is no general-purpose installer or verified recovery procedure. Bootloader or storage changes can make the phone unbootable or erase data. Verify the exact model, backups and recovery path before changing a device.

## Documentation

- [Measured S10+ hardware status and build details](Documentation/beyond2lte/STATUS.md)
- [S10+ bring-up evidence](Documentation/beyond2lte/bringup-evidence.md)
- [Device-specific recovery material](Documentation/beyond2lte/recovery/)
- [General Linux kernel README](README)

## Credits and license

The Exynos 9820 foundation builds on [chiffathefox/exynos-9820-mainline-linux](https://github.com/chiffathefox/exynos-9820-mainline-linux). Wi-Fi work used [LineageOS/android_kernel_samsung_exynos9820](https://github.com/LineageOS/android_kernel_samsung_exynos9820/tree/a412cfaf86b980ce35cae96a35f678c8ea551575) and [pulpoff/linux-beyond0lte](https://github.com/pulpoff/linux-beyond0lte/tree/3c142b4624f3caf9cb0ee90324ef7d62616d2a64) as references. Thanks to [butztill/s10-linux](https://github.com/butztill/s10-linux/tree/d8135b2465b3a9f1c0e7f1f6be71451ad185b303) for S10 bring-up work. Results from other devices do not establish S10+ support. Retain authorship, SPDX identifiers and applicable license terms when adapting code.

Kernel licensing information: [COPYING](COPYING).
