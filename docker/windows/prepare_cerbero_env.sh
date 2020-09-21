#! /bin/bash

set -eux

cd C:/
git clone -b ${DEFAULT_BRANCH} https://gitlab.freedesktop.org/gstreamer/cerbero.git
cd cerbero

echo 'local_sources="C:/cerbero/cerbero-sources"' > localconf.cbc
echo 'home_dir="C:/cerbero/cerbero-build"' >> localconf.cbc
echo 'vs_install_path = "C:/BuildTools"' >> localconf.cbc
echo 'vs_install_version = "vs15"' >> localconf.cbc

# Fetch all bootstrap requirements
./cerbero-uninstalled -t -c localconf.cbc -c config/win64.cbc fetch-bootstrap
# Fetch all package requirements for a mingw gstreamer build
./cerbero-uninstalled -t -c localconf.cbc -c config/win64.cbc fetch-package gstreamer-1.0
# Fetch all package requirements for a visualstudio gstreamer build
./cerbero-uninstalled -t -v visualstudio -c localconf.cbc -c config/win64.cbc fetch-package gstreamer-1.0

# Fixup the MSYS installation
./cerbero-uninstalled -t -c localconf.cbc -c config/win64.cbc bootstrap -y --build-tools=no --toolchains=no --offline

# Delete mingw toolchain binary tarball
rm /c/cerbero/cerbero-sources/mingw-*.tar.xz
# Wipe visualstudio package dist, sources, logs, and the build tools recipes
./cerbero-uninstalled -t -v visualstudio -c localconf.cbc -c config/win64.cbc wipe --force --build-tools
# clean the localconf
rm /c/cerbero/localconf.cbc
