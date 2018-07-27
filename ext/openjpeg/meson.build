openjpeg_sources = [
  'gstopenjpeg.c',
  'gstopenjpegdec.c',
  'gstopenjpegenc.c',
]

openjpeg_cargs = []

if get_option('openjpeg').disabled()
  subdir_done()
endif

openjpeg_dep = dependency('libopenjp2', version : '>=2.1', required : false)
if not openjpeg_dep.found()
  # Fallback to v1.5
  openjpeg_dep = dependency('libopenjpeg1', required : false)
  openjpeg_cargs += ['-DHAVE_OPENJPEG_1']
endif
if not openjpeg_dep.found() and get_option('openjpeg').enabled()
  error('openjpeg plugin enabled, but neither libopenjp2 nor libopenjpeg1 not found')
endif

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
endif
