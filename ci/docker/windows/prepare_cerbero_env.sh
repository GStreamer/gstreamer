#! /bin/bash

set -eux

cd C:/
git clone -b ${DEFAULT_BRANCH} https://gitlab.freedesktop.org/gstreamer/cerbero.git C:/cerbero
cd C:/cerbero

echo 'local_sources="C:/cerbero/cerbero-sources"' > localconf.cbc
echo 'home_dir="C:/cerbero/cerbero-build"' >> localconf.cbc
echo 'vs_install_path = "C:/BuildTools"' >> localconf.cbc
echo 'vs_install_version = "vs17"' >> localconf.cbc

# Fetch all bootstrap requirements
./cerbero-uninstalled -t  -v visualstudio -c localconf.cbc -c config/win64.cbc fetch-bootstrap --jobs=4
# Fetch all package requirements for a mingw gstreamer build
./cerbero-uninstalled -t -c localconf.cbc -c config/win64.cbc fetch-package --jobs=4 gstreamer-1.0
# Fetch all package requirements for a visualstudio gstreamer build
./cerbero-uninstalled -t -v visualstudio -c localconf.cbc -c config/win64.cbc fetch-package --jobs=4 gstreamer-1.0

# Fixup the MSYS installation
./cerbero-uninstalled -t -c localconf.cbc -c config/win64.cbc bootstrap -y --build-tools=no --toolchains=no --offline

# Wipe visualstudio package dist, sources, logs, and the build tools recipes
./cerbero-uninstalled -t -v visualstudio -c localconf.cbc -c config/win64.cbc wipe --force --build-tools
# Vendored sources get confused with hard links
rm -rvf /c/cerbero/cerbero-sources/*/cargo-vendor
# clean the localconf
rm -v /c/cerbero/localconf.cbc
