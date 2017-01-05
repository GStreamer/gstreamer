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
pkgconf.set('gstlibdir', join_paths(meson.build_root(), libgst.outdir()))
pkgconf.set('baselibdir', join_paths(meson.build_root(), gst_base.outdir()))
pkgconf.set('controllerlibdir', join_paths(meson.build_root(), gst_controller.outdir()))
pkgconf.set('netlibdir', join_paths(meson.build_root(), gst_net.outdir()))

pkg_install_dir = '@0@/pkgconfig'.format(get_option('libdir'))

pkg_files = ['gstreamer-base',
  'gstreamer-controller',
  'gstreamer-net',
  'gstreamer'
]

if host_machine.system() != 'windows'
    pkgconf.set('checklibdir', join_paths(meson.build_root(), gst_check.outdir()))
    pkg_files += ['gstreamer-check']
endif

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
