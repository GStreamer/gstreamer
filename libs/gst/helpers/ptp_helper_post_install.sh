#!/bin/sh
# Meson install script for gst-ptp-helper
# Fails silently at the moment if setting permissions/capabilities doesn't work
helpers_install_dir="$1"
with_ptp_helper_permissions="$2"
setcap="$3"

ptp_helper="$MESON_INSTALL_DESTDIR_PREFIX/$helpers_install_dir/gst-ptp-helper"

case "$with_ptp_helper_permissions" in
  setuid-root)
    echo "$0: permissions before: "
    ls -l "$ptp_helper"
    chown root "$ptp_helper" || true
    chmod u+s "$ptp_helper" || true
    echo "$0: permissions after: "
    ls -l "$ptp_helper"
    ;;
  capabilities)
    echo "Calling $setcap cap_net_bind_service,cap_net_admin+ep $ptp_helper"
    $setcap cap_net_bind_service,cap_net_admin+ep "$ptp_helper" || true
    ;;
  *)
    echo "$0 ERROR: unexpected permissions value '$with_ptp_helper_permissions'";
    exit 2;
esac
