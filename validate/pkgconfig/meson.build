pkgconf = configuration_data()

pkgconf.set('prefix', get_option('prefix'))
pkgconf.set('exec_prefix', '${prefix}')
pkgconf.set('libdir', '${prefix}/@0@'.format(get_option('libdir')))
pkgconf.set('includedir', '${prefix}/@0@'.format(get_option('includedir')))
pkgconf.set('GST_API_VERSION', apiversion)
pkgconf.set('VERSION', gst_version)

# needed for generating -uninstalled.pc files
pkgconf.set('abs_top_builddir', join_paths(meson.current_build_dir(), '..'))
pkgconf.set('abs_top_srcdir', join_paths(meson.current_source_dir(), '..'))
pkgconf.set('validatelibdir', join_paths(meson.build_root(), gstvalidate.outdir()))

pkg_install_dir = '@0@/pkgconfig'.format(get_option('libdir'))

configure_file(input : 'gst-validate.pc.in',
  output : 'gst-validate-1.0.pc',
  configuration : pkgconf,
  install_dir : pkg_install_dir)

configure_file(input : 'gst-validate-uninstalled.pc.in',
  output : 'gst-validate-1.0-uninstalled.pc',
  configuration : pkgconf)
