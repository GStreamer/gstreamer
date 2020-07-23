pkgconf = configuration_data()

pkgconf.set('prefix', join_paths(get_option('prefix')))
pkgconf.set('exec_prefix', '${prefix}')
pkgconf.set('libdir', '${prefix}/@0@'.format(get_option('libdir')))
pkgconf.set('includedir', '${prefix}/@0@'.format(get_option('includedir')))
pkgconf.set('GST_API_VERSION', api_version)
pkgconf.set('VERSION', gst_version)
pkgconf.set('LIBM', libm.found() ? '-lm' : '')
pkgconf.set('ZLIB_LIBS', zlib_dep.found() ? '-lz' : '')
pkgconf.set('ORC_PC', have_orcc ? 'orc-0.4' : '')

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

# XXX: requires the meson.build to be parsed/executed after gst-libs/gl/meson.build
if build_gstgl
  gllibdir = join_paths(meson.build_root(), gstgl.outdir())
  pkgconf.set('gllibdir', gllibdir)
  pkgconf.set('glliblinkerflag', '-L' + gllibdir)
  pkg_files += ['gstreamer-gl', 'gstreamer-gl-prototypes']
  pkgconf.set('GL_APIS', ' '.join(enabled_gl_apis))
  pkgconf.set('GL_WINDOWS', ' '.join(enabled_gl_winsys))
  pkgconf.set('GL_PLATFORMS', ' '.join(enabled_gl_platforms))
  pkgconf.set('gl_lib_name', 'gl')
  gl_proto_requires = ''
  gl_proto_libs = ''
  if enabled_gl_apis.contains('gl')
    if gl_dep.type_name() == 'pkgconfig'
      gl_proto_requires += ' gl'
    elif gl_dep.type_name() == 'library'
      gl_proto_libs += ' -lGL'
    elif gl_dep.type_name() == 'appleframework'
      gl_proto_libs += ' -framework OpenGL'
    endif
  endif
  if enabled_gl_apis.contains('gles2')
    if gles2_dep.type_name() == 'pkgconfig'
      gl_proto_requires += ' glesv2'
    elif gles2_dep.type_name() == 'library'
      gl_proto_libs += ' -lGLESv2'
    elif gles2_dep.type_name() == 'appleframework'
      gl_proto_libs += ' -framework OpenGLES'
    endif
  endif
  pkgconf.set('GL_PROTO_EXTRA_REQUIRES', gl_proto_requires)
  pkgconf.set('GL_PROTO_EXTRA_LIBS', gl_proto_libs)
  if enabled_gl_platforms.contains('egl')
    pkg_files += ['gstreamer-gl-egl']
    if egl_dep.type_name() == 'pkgconfig'
      pkgconf.set('GL_EGL_EXTRA_REQUIRES', 'egl')
      pkgconf.set('GL_EGL_EXTRA_LIBS', '')
    elif egl_dep.type_name() == 'library'
      pkgconf.set('GL_EGL_EXTRA_REQUIRES', '')
      pkgconf.set('GL_EGL_EXTRA_LIBS', '-lEGL')
    endif
  endif
  if enabled_gl_winsys.contains('x11')
    pkg_files += ['gstreamer-gl-x11']
  endif
  if enabled_gl_winsys.contains('wayland')
    pkg_files += ['gstreamer-gl-wayland']
  endif
else
  pkgconf.set('glliblinkerflag', '')
  pkgconf.set('gl_lib_name', '')
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
