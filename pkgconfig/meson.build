pkgconf = configuration_data()

pkgconf.set('prefix', join_paths(get_option('prefix')))
pkgconf.set('exec_prefix', '${prefix}')
pkgconf.set('libdir', '${prefix}/@0@'.format(get_option('libdir')))
pkgconf.set('includedir', '${prefix}/@0@'.format(get_option('includedir')))
pkgconf.set('GST_API_VERSION', api_version)
pkgconf.set('VERSION', gst_version)

# needed for generating -uninstalled.pc files
pkgconf.set('abs_top_builddir', join_paths(meson.current_build_dir(), '..'))
pkgconf.set('abs_top_srcdir', join_paths(meson.current_source_dir(), '..'))
pkgconf.set('audiolibdir', join_paths(meson.build_root(), gstbadaudio.outdir()))
pkgconf.set('transcoderlibdir', join_paths(meson.build_root(), gst_transcoder.outdir()))
pkgconf.set('codecparserslibdir', join_paths(meson.build_root(), gstcodecparsers.outdir()))
pkgconf.set('insertbinlibdir', join_paths(meson.build_root(), gstinsertbin.outdir()))
pkgconf.set('mpegtslibdir', join_paths(meson.build_root(), gstmpegts.outdir()))
pkgconf.set('playerlibdir', join_paths(meson.build_root(), gstplayer.outdir()))
pkgconf.set('basecamerabinsrclibdir', join_paths(meson.build_root(), gstbasecamerabin.outdir()))
pkgconf.set('photographylibdir', join_paths(meson.build_root(), gstphotography.outdir()))
pkgconf.set('webrtclibdir', join_paths(meson.build_root(), gstwebrtc.outdir()))

pkg_install_dir = '@0@/pkgconfig'.format(get_option('libdir'))

pkg_libs = [
  'bad-audio',
  'codecparsers',
  'insertbin',
  'mpegts',
  'photography',
  'player',
  'plugins-bad',
  'sctp',
  'transcoder',
  'webrtc',
]

#if use_wayland
#    pkgconf.set('waylandlibdir', join_paths(meson.build_root(), gstwayland.outdir()))
#    pkg_libs += 'wayland'
#endif

if gstvulkan_dep.found()
  pkgconf.set('vulkanlibdir', join_paths(meson.build_root(), gstvulkan.outdir()))
  pkgconf.set('VULKAN_WINSYS', ' '.join(enabled_vulkan_winsys))
  pkg_libs += 'vulkan'

  if enabled_vulkan_winsys.contains('xcb')
    pkg_libs += 'vulkan-xcb'
  endif
  if enabled_vulkan_winsys.contains('wayland')
    pkg_libs += 'vulkan-wayland'
  endif
endif

foreach p : pkg_libs
  infile = 'gstreamer-@0@.pc.in'.format(p)
  outfile = 'gstreamer-@0@-@1@.pc'.format(p, api_version)
  configure_file(input : infile,
    output : outfile,
    configuration : pkgconf,
    install_dir : pkg_install_dir)

  infile = 'gstreamer-@0@-uninstalled.pc.in'.format(p)
  outfile = 'gstreamer-@0@-@1@-uninstalled.pc'.format(p, api_version)
  configure_file(input : infile,
    output : outfile,
    configuration : pkgconf)
endforeach
