pkgconf = configuration_data()

pkgconf.set('prefix', get_option('prefix'))
pkgconf.set('exec_prefix', '${prefix}')
pkgconf.set('libdir', '${prefix}/@0@'.format(get_option('libdir')))
pkgconf.set('includedir', '${prefix}/@0@'.format(get_option('includedir')))
pkgconf.set('GST_API_VERSION', api_version)
pkgconf.set('VERSION', gst_version)

pkg_install_dir = '@0@/pkgconfig'.format(get_option('libdir'))

# FIXME: -uninstalled.pc files (if still needed?)
configure_file(input : 'gstreamer-rtsp-server.pc.in',
  output : 'gstreamer-rtsp-server-1.0.pc',
  configuration : pkgconf,
  install_dir : pkg_install_dir,
)
