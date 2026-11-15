# S10+ kernel: build, share and use

The same source can be built on an x86-64 Linux machine or directly on the
S10+ running ARM64 Linux. The phone is a development machine too; use whichever
machine has the tools, disk space and time. No GitHub Actions are required.

This guide targets the **Exynos Galaxy S10+ SM-G975F (`beyond2lte`)**. The
kernel is experimental. A successful build does not establish that it boots.
The files are not an Android boot image or a complete operating system.

## 1. Build from source

Install Git, GNU make and a C build environment, Clang/LLVM and LLD 17, flex,
bison, bc, Python 3, Perl, `pahole` (dwarves), OpenSSL and libelf development
headers, kmod (`depmod`), cpio, zstd and xz. On Ubuntu versions offering these
packages, for example:

```sh
sudo apt install git build-essential clang-17 lld-17 llvm-17 flex bison bc \
  python3 perl dwarves libssl-dev libelf-dev kmod cpio zstd xz-utils
```

On the phone, install equivalent packages using its distribution's package
manager. The commands below use LLVM 17 on both ARM64 and x86-64; other compiler
versions may work but have not been verified for this release. A native build
on the phone can take much longer. Keep `/sbin` and `/usr/sbin` in `PATH` so
that `modules_install` can find `depmod`.

Choose a source commit or release tag. In an existing checkout, fetch first;
keep your uncommitted work safe rather than overwriting it. A clean checkout
makes the exact source easier to identify.

```sh
git clone https://github.com/omnipha/linux-samsung-s10-exynos9820.git
cd linux-samsung-s10-exynos9820
git switch --detach 4debea4f7a67a87f5ef014070af5a3fde514efe1  # or another commit/tag
git status --short
git rev-parse HEAD
export PATH="$PATH:/usr/sbin:/sbin"
make O=out ARCH=arm64 LLVM=-17 beyond2lte_defconfig
make -j"$(nproc)" O=out ARCH=arm64 LLVM=-17 Image modules dtbs
```

Reduce `-j` if the phone runs low on memory. The artifacts are:

- `out/arch/arm64/boot/Image`: ARM64 kernel.
- `out/arch/arm64/boot/dts/exynos/exynos9820-beyond2lte.dtb`: S10+ device tree.
- `out/.config`: the actual build configuration.
- `out/include/config/kernel.release`: the exact version used by the modules.

A different source commit, configuration, compiler or available Git tags can
change the generated version and output. Publish only builds from committed
source; uncommitted changes cannot be reproduced from a public commit. Use an
Image, DTB, modules and initramfs built for the **same** kernel. The `.config` and commit allow a build
to be repeated; bit-for-bit reproducibility has not yet been established.

## 2. Package a build

After the build above, the following stages the modules without touching the
running machine's `/lib/modules` and creates three files in `artifacts/`.
It works on a build host or on the phone itself. `mktemp` creates a new staging
folder; it does not delete or overwrite the phone's existing kernels.

```sh
COMMIT=$(git rev-parse HEAD)
SHORT=$(git rev-parse --short=12 HEAD)
REL=$(cat out/include/config/kernel.release)
STAGE=$(mktemp -d "${TMPDIR:-/tmp}/s10-kernel.XXXXXXXX")
make O=out ARCH=arm64 LLVM=-17 INSTALL_MOD_PATH="$STAGE" modules_install
rm -f "$STAGE/lib/modules/$REL/build" "$STAGE/lib/modules/$REL/source"
mkdir -p "$STAGE/boot" artifacts
install -m 0644 out/arch/arm64/boot/Image "$STAGE/boot/Image"
install -m 0644 out/arch/arm64/boot/dts/exynos/exynos9820-beyond2lte.dtb \
  "$STAGE/boot/exynos9820-beyond2lte.dtb"
cp out/.config "artifacts/s10-kernel-$SHORT.config"
tar -C "$STAGE" -cJf "artifacts/s10-kernel-$SHORT.tar.xz" boot lib
(cd artifacts && sha256sum "s10-kernel-$SHORT.tar.xz" "s10-kernel-$SHORT.config" \
  > "s10-kernel-$SHORT.sha256")
printf 'commit=%s\nkernel_release=%s\nstaging=%s\n' "$COMMIT" "$REL" "$STAGE"
```

The archive contains the Image, DTB and the matching `lib/modules/<release>`
tree. It does **not** contain firmware, calibration data, an initramfs or
credentials. The config and checksum file accompany it. Inspect the output and
remove the temporary staging directory when finished.

## 3. Download or publish a release

[GitHub Releases](https://github.com/omnipha/linux-samsung-s10-exynos9820/releases)
holds downloadable builds, separate from Git source history. The first build
is an **experimental pre-release**, not yet boot-tested. Download in a new
working directory, either via the browser or with [GitHub CLI](https://cli.github.com/)
(`gh`), installed separately. GitHub CLI may ask you to log in first:

```sh
mkdir -p s10-download && cd s10-download
gh release download kernel-4debea4f7a67 \
  --repo omnipha/linux-samsung-s10-exynos9820
sha256sum -c s10-kernel-4debea4f7a67.sha256 && \
  tar -tJf s10-kernel-4debea4f7a67.tar.xz && \
  tar -xJf s10-kernel-4debea4f7a67.tar.xz
```

The SHA-256 check detects accidental corruption; it does not authenticate the
publisher. Compare the full commit and test status in the release notes.
Do not extract the archive directly over `/` or `/boot`.

A maintainer can publish a newly packaged build manually, without CI:

```sh
gh release create "kernel-$SHORT" --repo omnipha/linux-samsung-s10-exynos9820 \
  --target "$COMMIT" --prerelease \
  --title "S10+ kernel build $SHORT" \
  --notes "Commit $COMMIT; kernel $REL; built with LLVM 17. Not boot-tested." \
  "artifacts/s10-kernel-$SHORT.tar.xz" "artifacts/s10-kernel-$SHORT.config" \
  "artifacts/s10-kernel-$SHORT.sha256"
```

Push the source commit first. Record compiler version, configuration and actual
build and device-test results in the notes. Check whether the tag already exists
before creating a release; do not overwrite somebody else's release.

## 4. Use on an existing Linux S10+

A build made locally and a verified download have the same logical inputs:
`Image`, DTB and matching module tree. Neither is a complete bootable installation.
On the tested device, Samsung's bootloader starts U-Boot, which starts
systemd-boot from the `/boot` EFI partition. The selected boot entry specifies
kernel, DTB, initramfs and device-specific kernel options. Other installations
may differ. Do not write an `Image` directly to an Android boot partition.

1. Check `findmnt /boot`, `df -h /boot`, `/boot/loader/loader.conf` and the
   entries in `/boot/loader/entries/`. Back up the working kernel, DTB,
   initramfs, modules and loader configuration off the phone. Verify the actual
   recovery path. The test phone's EFI partition has previously been nearly full.
2. Copy the new Image and DTB to **new versioned filenames** under the mounted
   `/boot`; never overwrite the active or fallback files. Copy the matching
   `lib/modules/<release>` directory to `/lib/modules/` without overwriting an
   existing version, and run `sudo depmod -a <release>`. Check hashes after
   transfer. Build directly on the phone or transfer the files from the host.
   For the verified download above, this stages the files on an already running
   Linux system. It does **not** select or boot the new kernel:

   ```sh
   (
   set -euo pipefail
   REL=$(basename "$(find lib/modules -mindepth 1 -maxdepth 1 -type d)")
   test "$(printf '%s\n' "$REL" | wc -l)" -eq 1
   test "$(findmnt -n -o TARGET --target /boot)" = /boot
   test -f boot/Image && test -f boot/exynos9820-beyond2lte.dtb
   test -d "lib/modules/$REL"
   test ! -e "/boot/vmlinuz-$REL" && test ! -L "/boot/vmlinuz-$REL"
   test ! -e "/boot/dtbs/exynos/$REL.dtb" && test ! -L "/boot/dtbs/exynos/$REL.dtb"
   test ! -e "/lib/modules/$REL" && test ! -L "/lib/modules/$REL"
   sudo install -D -m 0644 boot/Image "/boot/vmlinuz-$REL"
   sudo install -D -m 0644 boot/exynos9820-beyond2lte.dtb "/boot/dtbs/exynos/$REL.dtb"
   sudo mkdir -p "/lib/modules/$REL"
   sudo cp -a --no-preserve=ownership "lib/modules/$REL/." "/lib/modules/$REL/"
   sudo depmod -a "$REL"
   cmp boot/Image "/boot/vmlinuz-$REL"
   cmp boot/exynos9820-beyond2lte.dtb "/boot/dtbs/exynos/$REL.dtb"
   find "lib/modules/$REL/kernel" -type f -name '*.ko*' -print | while IFS= read -r f; do
     cmp "$f" "/$f"
   done
   )
   ```

   These commands assume GNU `cp` and `install`. If the phone's tools do not
   support them, use equivalent commands or transfer from a build host. The
   copy must fit in `/boot`. A failure may leave partial versioned files; inspect
   them before retrying. Retain a backup before any boot-menu changes.
3. Prepare a compatible initramfs for that exact release using the target
   installation's procedure. There is no universal initramfs command: it must
   match the existing boot setup, include this release's modules, and use the
   compression accepted by that setup. Early-boot modules and device-specific firmware may
   be required there, not only on the root filesystem. Do not publish the
   phone's firmware, calibration data or private initramfs. On the test phone,
   an XZ initramfs using CRC64 failed; see the recovery guide for the measured
   CRC32 limitation.
4. Configure the existing system's bootloader to use the new Image, DTB and
   initramfs together while retaining a selectable working fallback. Use that
   system's own kernel options and UUIDs. Only mark the build boot-tested after
   an actual device test. Do not make an untested kernel the default. The test
   phone has three existing entries; its recovery notes describe their roles.

The [device-specific recovery notes](recovery/README.md) describe the tested
boot layout and limitations; their rollback procedure itself is not yet tested.
This guide does not claim a universal installer or verified Android restore.
