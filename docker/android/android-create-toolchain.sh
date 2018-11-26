#!/bin/bash

#
# Copyright 2018 Collabora ltd.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, see <http://www.gnu.org/licenses/>.
#
# Author: Xavier Claessens <xavier.claessens@collabora.com>
#

set -eu

arch=$1
api=$2
toolchain_path=/android-toolchain-$arch-$api

# Create standalone toolchains
/android-ndk/build/tools/make_standalone_toolchain.py --arch $arch --api $api --install-dir $toolchain_path

# Create a cross file that can be passed to meson
cat > android_${arch}_${api}.txt <<- EOM
[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = '${arch}'
endian = 'little'

[properties]
c_args = ['--sysroot', '${toolchain_path}/sysroot']
cpp_args = ['--sysroot', '${toolchain_path}/sysroot']

[binaries]
c = '${toolchain_path}/bin/aarch64-linux-android-clang'
cpp = '${toolchain_path}/bin/aarch64-linux-android-clang++'
ar = '${toolchain_path}/bin/aarch64-linux-android-ar'
strip = '${toolchain_path}/bin/aarch64-linux-android-strip'
pkgconfig = 'false'
EOM
