openjpeg_sources = [
  'gstopenjpeg.c',
  'gstopenjpegdec.c',
  'gstopenjpegenc.c',
]

openjpeg_cargs = []

if get_option('openjpeg').disabled()
  subdir_done()
endif

openjpeg_dep = dependency('libopenjp2', version : '>=2.2',
                          fallback : ['libopenjp2', 'libopenjp2_dep'],
                          required : get_option('openjpeg'))

if openjpeg_dep.found()
  gstopenjpeg = library('gstopenjpeg',
    openjpeg_sources,
    c_args : gst_plugins_bad_args + openjpeg_cargs,
    link_args : noseh_link_args,
    include_directories : [configinc],
    dependencies : [gst_dep, gstvideo_dep, openjpeg_dep,
		    gstcodecparsers_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstopenjpeg, install_dir : plugins_pkgconfig_install_dir)
  plugins += [gstopenjpeg]
endif
