pkgconf = configuration_data()

pkgconf.set('prefix', get_option('prefix'))
pkgconf.set('exec_prefix', '${prefix}')
pkgconf.set('libdir', '${prefix}/@0@'.format(get_option('libdir')))
pkgconf.set('includedir', '${prefix}/@0@'.format(get_option('includedir')))
pkgconf.set('GST_API_VERSION', api_version)
pkgconf.set('VERSION', gst_version)

pkg_install_dir = '@0@/pkgconfig'.format(get_option('libdir'))

pkg_files = [
  'gstreamer-allocators',
  'gstreamer-app',
  'gstreamer-audio',
  'gstreamer-fft',
  'gstreamer-pbutils',
  'gstreamer-plugins-base',
  'gstreamer-riff',
  'gstreamer-rtp',
  'gstreamer-rtsp',
  'gstreamer-sdp',
  'gstreamer-tag',
  'gstreamer-video',
]

# FIXME: -uninstalled.pc files (if still needed?)
foreach p : pkg_files
  infile = p + '.pc.in'
  outfile = p + '-1.0.pc'
  configure_file(input : infile,
    output : outfile,
    configuration : pkgconf,
    install_dir : pkg_install_dir)
endforeach
