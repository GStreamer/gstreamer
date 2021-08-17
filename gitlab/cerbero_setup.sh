#!/bin/bash

set -ex

show_ccache_sum() {
    if [[ -n ${HAVE_CCACHE} ]]; then
        ccache -s
    fi
}

# XXX: This is copied and modified from the cerbero-uninstalled script
# Use `mount` to get a list of MSYS mount points that the MSYS shell uses.
# That's our reference point for translating from MSYS paths to Win32 paths.
# We assume that the MSYS mount point directories are only in the filesystem
# root. This will break if people add their own custom mount points beyond what
# MSYS automatically creates, which is highly unlikely.
#
# /d -> d:/
# /c -> c:/
# /d/projects/cerbero -> d:/projects/cerbero/
# /home/USERNAME/cerbero -> C:\\MinGW\\msys\\1.0/home/USERNAME/
# /mingw -> C:\\MinGW/
# /mingw/bin/foobar -> C:\\MinGW\\bin/foobar/
# /tmp/baz -> C:\\Users\\USERNAME\\AppData\\Local\\Temp/baz/
msys_dir_to_win32() {
    set -e
    local msys_path stripped_path mount_point path mounted_path
    # If the path is already a native path, just return that
    if [[ $1 == ?:/* ]] || [[ $1 == ?:\\* ]]; then
      echo $1
      return
    fi
    # Convert /c or /mingw etc to /c/ or /mingw/ etc; gives us a necessary
    # anchor to split the path into components
    msys_path="$1/"
    # Strip leading slash
    stripped_path="${msys_path#/}"
    # Get the first path component, which may be a mount point
    mount_point="/${stripped_path%%/*}"
    # Get the path inside the mountp oint
    path="/${stripped_path#*/}"
    mounted_path="$(mount | sed -n "s|\(.*\) on $mount_point type.*|\1|p")"
    # If it's not a mounted path (like /c or /tmp or /mingw), then it's in the
    # general MSYS root mount
    if [[ -z $mounted_path ]]; then
        mounted_path="$(mount | sed -n "s|\(.*\) on / type.*|\1|p")"
        path="$1"
    fi
    echo ${mounted_path}${path%/}
}

# Print the working directory in the native OS path format, but with forward
# slashes
pwd_native() {
    if [[ -n "$MSYSTEM" ]]; then
        msys_dir_to_win32 "$(pwd)"
    else
        pwd
    fi
}

fix_build_tools() {
    if [[ $(uname) == Darwin ]]; then
        # Bison needs these env vars set for the build-tools prefix to be
        # relocatable, and we only build it on macOS. On Linux we install it
        # using the package manager, and on Windows we use the MSYS Bison.
        export M4="$(pwd)/${CERBERO_HOME}/build-tools/bin/m4"
        export BISON_PKGDATADIR="$(pwd)/${CERBERO_HOME}/build-tools/share/bison"
    fi
}

# Produces runtime and devel tarball packages for linux/android or .pkg for macos
cerbero_package_and_check() {
    $CERBERO $CERBERO_ARGS package --offline ${CERBERO_PACKAGE_ARGS} -o "$(pwd_native)" gstreamer-1.0

    # Run gst-inspect-1.0 for some basic checks. Can't do this for cross-(android|ios)-universal, of course.
    if [[ $CONFIG != *universal* ]]; then
        $CERBERO $CERBERO_ARGS run $CERBERO_RUN_WRAPPER gst-inspect-1.0$CERBERO_RUN_SUFFIX --version
        $CERBERO $CERBERO_ARGS run $CERBERO_RUN_WRAPPER gst-inspect-1.0$CERBERO_RUN_SUFFIX
    fi

    show_ccache_sum
}

cerbero_before_script() {
    pwd
    ls -lha

    # Copy cerbero git repo stored on the image
    cp -a "${CERBERO_HOST_DIR}/.git" .
    git checkout .
    git status

    # If there's no cerbero-sources directory in the runner cache, copy it from
    # the image cache
    if ! [[ -d ${CERBERO_SOURCES} ]]; then
        time cp -a "${CERBERO_HOST_DIR}/${CERBERO_SOURCES}" .
    fi
    du -sch "${CERBERO_SOURCES}" || true

    echo "home_dir = \"$(pwd_native)/${CERBERO_HOME}\"" > localconf.cbc
    echo "local_sources = \"$(pwd_native)/${CERBERO_SOURCES}\"" >> localconf.cbc
    if [[ $CONFIG == win??.cbc ]]; then
        # Visual Studio 2017 build tools install path
        echo 'vs_install_path = "C:/BuildTools"' >> localconf.cbc
        echo 'vs_install_version = "vs15"' >> localconf.cbc
    fi
    cat localconf.cbc

    time ./cerbero-uninstalled --self-update manifest.xml

    # GitLab runner does not always wipe the image after each job, so do that
    # to ensure we don't have any leftover data from a previous job such as
    # a dirty builddir, or tarballs/pkg files, leftover files from an old
    # cerbero commit, etc. Skip the things we actually need to keep.
    time git clean -xdff -e cerbero_setup.sh -e manifest.xml -e localconf.cbc -e "${CERBERO_SOURCES}"
}

cerbero_script() {
    show_ccache_sum

    $CERBERO $CERBERO_ARGS show-config
    $CERBERO $CERBERO_ARGS fetch-bootstrap --jobs=4
    $CERBERO $CERBERO_ARGS fetch-package --jobs=4 --deps gstreamer-1.0
    du -sch "${CERBERO_SOURCES}" || true

    $CERBERO $CERBERO_ARGS fetch-cache --branch "${GST_UPSTREAM_BRANCH}"

    if [[ -n ${CERBERO_OVERRIDDEN_DIST_DIR} && -d "${CERBERO_HOME}/dist/${ARCH}" ]]; then
        mkdir -p "${CERBERO_OVERRIDDEN_DIST_DIR}"
        time rsync -aH "${CERBERO_HOME}/dist/${ARCH}/" "${CERBERO_OVERRIDDEN_DIST_DIR}"
    fi

    $CERBERO $CERBERO_ARGS bootstrap --offline --system=no
    fix_build_tools

    cerbero_package_and_check
}

cerbero_deps_script() {
    show_ccache_sum

    $CERBERO $CERBERO_ARGS show-config
    $CERBERO $CERBERO_ARGS fetch-bootstrap --jobs=4
    $CERBERO $CERBERO_ARGS fetch-package --jobs=4 --deps gstreamer-1.0
    $CERBERO $CERBERO_ARGS bootstrap --offline --system=no
    $CERBERO $CERBERO_ARGS build-deps --offline \
        gstreamer-1.0 gst-plugins-base-1.0 gst-plugins-good-1.0 \
        gst-plugins-bad-1.0 gst-plugins-ugly-1.0 gst-rtsp-server-1.0 \
        gst-libav-1.0 gst-devtools-1.0 gst-editing-services-1.0 libnice

    if [[ -n ${CERBERO_OVERRIDDEN_DIST_DIR} ]]; then
        mkdir -p "${CERBERO_HOME}/dist/${ARCH}"
        time rsync -aH "${CERBERO_OVERRIDDEN_DIST_DIR}/" "${CERBERO_HOME}/dist/${ARCH}"
    fi

    # Check that the env var is set. Don't expand this protected variable by
    # doing something silly like [[ -n ${CERBERO_...} ]] because it will get
    # printed in the CI logs due to set -x
    if env | grep -q -e CERBERO_PRIVATE_SSH_KEY; then
        time $CERBERO $CERBERO_ARGS gen-cache --branch "${GST_UPSTREAM_BRANCH}"
        time $CERBERO $CERBERO_ARGS upload-cache --branch "${GST_UPSTREAM_BRANCH}"
    fi

    cerbero_package_and_check
}

# Run whichever function is asked of us
eval "$1"
