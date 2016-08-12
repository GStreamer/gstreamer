pkgconf = configuration_data()

pkgconf.set('prefix', get_option('prefix'))
pkgconf.set('exec_prefix', '${prefix}')
pkgconf.set('libdir', '${prefix}/@0@'.format(get_option('libdir')))
pkgconf.set('includedir', '${prefix}/@0@'.format(get_option('includedir')))
pkgconf.set('GST_API_VERSION', api_version)
pkgconf.set('VERSION', gst_version)

pkg_install_dir = '@0@/pkgconfig'.format(get_option('libdir'))

pkg_libs = [
  'bad-audio',
  'bad-base',
  'bad-video',
  'codecparsers',
  # Broken and not built currently
  #'gl',
  'insertbin',
  'mpegts',
  'player',
  'plugins-bad',
]

# FIXME: -uninstalled.pc files (if still needed?)
foreach p : pkg_libs
  infile = 'gstreamer-@0@.pc.in'.format(p)
  outfile = 'gstreamer-@0@-@1@.pc'.format(p, api_version)
  configure_file(input : infile,
    output : outfile,
    configuration : pkgconf,
    install_dir : pkg_install_dir)
endforeach
