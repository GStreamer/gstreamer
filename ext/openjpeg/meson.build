openjpeg_sources = [
  'gstopenjpeg.c',
  'gstopenjpegdec.c',
  'gstopenjpegenc.c',
]

openjpeg_cargs = []

max_supported_version = '<2.4'
supported_versions = [ '2.3', '2.2', '2.1' ]
foreach version: supported_versions
  openjpeg_dep = dependency('libopenjp2', version : ['>=' + version, max_supported_version],
      required : false)
  if openjpeg_dep.found()
    openjpeg_cargs += ['-DHAVE_OPENJPEG_' + version.underscorify()]
  endif
endforeach

if not openjpeg_dep.found()
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
