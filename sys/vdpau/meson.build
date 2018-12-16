vdpau_sources = [
  'gstvdpau.c',
  'gstvdputils.c',
  'gstvdpvideomemory.c',
  'gstvdpvideobufferpool.c',
  'gstvdpdevice.c',
  'gstvdpdecoder.c',
  'mpeg/gstvdpmpegdec.c',
  # 'h264/gsth264dpb.c',
  # 'h264/gstvdph264dec.c',
]

vdpau_dep = dependency('vdpau', required: get_option('vdpau'))

if vdpau_dep.found()
  if x11_dep.found()
    gstvdpau = library('gstvdpau',
      vdpau_sources,
      c_args: gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
      include_directories: [configinc],
      dependencies: [gstbase_dep, gstvideo_dep, gstcodecparsers_dep, vdpau_dep, x11_dep, libm],
      install: true,
      install_dir: plugins_install_dir,
    )
    pkgconfig.generate(gstvdpau, install_dir: plugins_pkgconfig_install_dir)
  elif get_option('vdpau').enabled()
    error('vdpau plugin was enabled but required X11 dependency was not found.')
  endif
endif
