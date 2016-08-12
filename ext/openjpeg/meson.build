openjpeg_sources = [
  'gstopenjpeg.c',
  'gstopenjpegdec.c',
  'gstopenjpegenc.c',
]

openjpeg_cargs = []

# Check for 2.1, then 2.0
openjpeg_dep = dependency('libopenjp2', version : '>=2.1', required : false)
if openjpeg_dep.found()
  openjpeg_cargs += ['-DHAVE_OPENJPEG_2_1']
else
  openjpeg_dep = dependency('libopenjp2', required : false)
  # Fallback to 1.5
  if not openjpeg_dep.found()
    openjpeg_dep = dependency('libopenjpeg1', required : false)
    openjpeg_cargs += ['-DHAVE_OPENJPEG_1']
  endif
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
endif
