#! /bin/bash

set -eux

# Clone `-b main gstreamer/cerbero` by default, but if running on a gstreamer
# branch in another namespace that has a corresponding cerbero branch by the
# same name, clone that instead.
clone_args="$(py -3 C:/get_cerbero_clone_args.py)"
echo "Cloning Cerbero using $clone_args"
git clone $clone_args C:/cerbero
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

# Fixup the MSYS2 installation
./cerbero-uninstalled -t -c localconf.cbc -c config/win64.cbc bootstrap -y --build-tools=no --toolchains=no --offline

# Wipe visualstudio package dist, sources, logs, and the build tools recipes
./cerbero-uninstalled -t -v visualstudio -c localconf.cbc -c config/win64.cbc wipe --force --build-tools
# Vendored sources get confused with hard links. This is not needed anyway,
# because cargo stores sources in ~/.cargo/registry/ for offline use.
rm -rf /c/cerbero/cerbero-sources/*/cargo-vendor
# clean the localconf
rm -v /c/cerbero/localconf.cbc
