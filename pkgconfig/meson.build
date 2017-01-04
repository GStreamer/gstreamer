pkgconf = configuration_data()

pkgconf.set('prefix', get_option('prefix'))
pkgconf.set('exec_prefix', '${prefix}')
pkgconf.set('libdir', '${prefix}/@0@'.format(get_option('libdir')))
pkgconf.set('includedir', '${prefix}/@0@'.format(get_option('includedir')))
pkgconf.set('GST_API_VERSION', api_version)
pkgconf.set('VERSION', gst_version)

# needed for generating -uninstalled.pc files
pkgconf.set('abs_top_builddir', join_paths(meson.current_build_dir(), '..'))
pkgconf.set('abs_top_srcdir', join_paths(meson.current_source_dir(), '..'))
pkgconf.set('allocatorslibdir', join_paths(meson.build_root(), gstallocators.outdir()))
pkgconf.set('applibdir', join_paths(meson.build_root(), gstapp.outdir()))
pkgconf.set('audiolibdir', join_paths(meson.build_root(), gstaudio.outdir()))
pkgconf.set('fftlibdir', join_paths(meson.build_root(), gstfft.outdir()))
pkgconf.set('pbutilslibdir', join_paths(meson.build_root(), pbutils.outdir()))
pkgconf.set('rifflibdir', join_paths(meson.build_root(), gstriff.outdir()))
pkgconf.set('rtplibdir', join_paths(meson.build_root(), gst_rtp.outdir()))
pkgconf.set('rtsplibdir', join_paths(meson.build_root(), gst_rtsp.outdir()))
pkgconf.set('sdplibdir', join_paths(meson.build_root(), gstsdp.outdir()))
pkgconf.set('taglibdir', join_paths(meson.build_root(), gsttag.outdir()))
pkgconf.set('videolibdir', join_paths(meson.build_root(), gstvideo.outdir()))

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

  infile = p + '-uninstalled.pc.in'
  outfile = p + '-1.0-uninstalled.pc'
  configure_file(input : infile,
    output : outfile,
    configuration : pkgconf)
endforeach
