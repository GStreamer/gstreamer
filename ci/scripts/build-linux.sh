#!/bin/bash
#
# Based on the build-linux.sh script from the Mutter project:
# https://gitlab.gnome.org/GNOME/mutter/-/blob/main/src/tests/kvm/build-linux.sh
#
# Script for building the Linux kernel from git. It aims to build a kernel image
# that is suitable for running in a virtual machine and is aimed to used for
# testing.
#
# Usage: build-linux.sh [REPO-URL] [BRANCH|TAG] [OUTPUT-FILE] [...CONFIGS]
#
# Where [..CONFIGS] can be any number of configuration options, e.g.
# --enable CONFIG_DRM_VKMS
#

set -e

# From scripts/subarch.include in linux
function get-subarch()
{
  uname -m | sed -e s/i.86/x86/ \
                 -e s/x86_64/x86/ \
                 -e s/sun4u/sparc64/ \
                 -e s/arm.*/arm/ -e s/sa110/arm/ \
                 -e s/s390x/s390/ -e s/parisc64/parisc/ \
                 -e s/ppc.*/powerpc/ -e s/mips.*/mips/ \
                 -e s/sh[234].*/sh/ -e s/aarch64.*/arm64/ \
                 -e s/riscv.*/riscv/
}

REPO="$1"
BRANCH_OR_TAG="$2"
IMAGE="$3"

ARCH=$(uname -m)
SUBARCH=$(get-subarch)

shift
shift
shift

# ./scripts/config  --enable CONFIG_VIDEO_VISL
CONFIGS=()
while [[ "x$1" != "x" ]]; do
  CONFIGS+=( "$1" )
  shift
done

echo Building Linux for $ARCH \($SUBARCH\)...

set -x

if [ -d linux ]; then
  pushd linux
  git fetch --depth=1 $REPO $BRANCH_OR_TAG
  git checkout FETCH_HEAD
else
  git clone --depth=1 --branch=$BRANCH_OR_TAG $REPO linux
  pushd linux
fi

# Apply visl patches until they are upstreamed
for patch in ../ci/docker/fedora/patches/*.patch; do
	patch -p1 < "${patch}"
done

make defconfig
sync
make kvm_guest.config

echo "Disabling unused features..."
./scripts/config \
  --disable USB \
  --disable SOUND \
  --disable SND \
  --disable NETDEVICES \
  --disable DRM \
  --disable INPUT \
  --disable I2C \
  --disable HID \
  --disable CRYPTO \
  --disable IPV6

echo Enabling ${CONFIGS[@]}...
./scripts/config ${CONFIGS[@]/#/--enable }

make olddefconfig
make -j8 WERROR=0

popd

TARGET_DIR="$(dirname "$IMAGE")"
mkdir -p "$TARGET_DIR"
mv linux/arch/$SUBARCH/boot/bzImage "$IMAGE"
mv linux/.config $TARGET_DIR/.config
rm -rf linux
