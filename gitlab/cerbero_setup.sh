set -ex

show_ccache_sum() {
    if [[ -n ${HAVE_CCACHE} ]]; then
        ccache -s
    fi
}

# Produces runtime and devel tarball packages for linux/android or .pkg for macos
cerbero_package_and_check() {
    $CERBERO $CERBERO_ARGS package --offline ${CERBERO_PACKAGE_ARGS} -o "$(pwd)" gstreamer-1.0

    # Run gst-inspect-1.0 for some basic checks. Can't do this for cross-(android|ios)-universal, of course.
    if [[ $CONFIG != *universal* ]]; then
        $CERBERO $CERBERO_ARGS run $CERBERO_RUN_WRAPPER gst-inspect-1.0$CERBERO_RUN_SUFFIX --version
        $CERBERO $CERBERO_ARGS run $CERBERO_RUN_WRAPPER gst-inspect-1.0$CERBERO_RUN_SUFFIX
    fi

    show_ccache_sum
}

cerbero_before_script() {
    # FIXME Wrong namespace
    # Workaround build-tools having hardcoded internal path
    pwd
    mkdir -p "../../gstreamer"
    ln -sf "$(pwd)" "../../gstreamer/cerbero"
    mkdir -p "../../${CI_PROJECT_NAMESPACE}"
    ln -sf "$(pwd)" "../../${CI_PROJECT_NAMESPACE}/cerbero"
    rsync -aH "${CERBERO_HOST_DIR}" .
    echo "home_dir = \"$(pwd)/${CERBERO_HOME}\"" >> localconf.cbc
    echo "local_sources = \"$(pwd)/${CERBERO_SOURCES}\"" >> localconf.cbc
    ./cerbero-uninstalled --self-update manifest.xml
}

cerbero_script() {
    show_ccache_sum

    $CERBERO $CERBERO_ARGS show-config
    $CERBERO $CERBERO_ARGS fetch-bootstrap --build-tools-only
    $CERBERO $CERBERO_ARGS fetch-package --deps gstreamer-1.0
    $CERBERO $CERBERO_ARGS fetch-cache --branch "${GST_UPSTREAM_BRANCH}"

    if [[ -n ${CERBERO_OVERRIDDEN_DIST_DIR} ]]; then
        test -d "${CERBERO_HOME}/dist/${ARCH}"
        mkdir -p "${CERBERO_OVERRIDDEN_DIST_DIR}"
        rsync -aH "${CERBERO_HOME}/dist/${ARCH}/" "${CERBERO_OVERRIDDEN_DIST_DIR}"
    fi

    $CERBERO $CERBERO_ARGS bootstrap --offline --build-tools-only
    cerbero_package_and_check
}

cerbero_deps_script() {
    show_ccache_sum

    $CERBERO $CERBERO_ARGS show-config
    $CERBERO $CERBERO_ARGS fetch-bootstrap --build-tools-only
    $CERBERO $CERBERO_ARGS fetch-package --deps gstreamer-1.0
    $CERBERO $CERBERO_ARGS bootstrap --offline --build-tools-only
    $CERBERO $CERBERO_ARGS build-deps --offline \
        gstreamer-1.0 gst-plugins-base-1.0 gst-plugins-good-1.0 \
        gst-plugins-bad-1.0 gst-plugins-ugly-1.0 gst-rtsp-server-1.0 \
        gst-libav-1.0 gst-devtools-1.0 gst-editing-services-1.0 libnice

    if [[ -n ${CERBERO_OVERRIDDEN_DIST_DIR} ]]; then
        mkdir -p "${CERBERO_HOME}/dist/${ARCH}"
        rsync -aH "${CERBERO_OVERRIDDEN_DIST_DIR}/" "${CERBERO_HOME}/dist/${ARCH}"
    fi

    if [[ -n ${CERBERO_PRIVATE_SSH_KEY} ]]; then
        $CERBERO $CERBERO_ARGS gen-cache --branch "${GST_UPSTREAM_BRANCH}"
        $CERBERO $CERBERO_ARGS upload-cache --branch "${GST_UPSTREAM_BRANCH}"
    fi

    cerbero_package_and_check
}

# Run whichever function is asked of us
eval "$1"
