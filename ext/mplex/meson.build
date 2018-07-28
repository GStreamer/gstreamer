# See: ext/mpeg2enc for note about mjpegtools dep
mjpegtools_dep = dependency('mjpegtools', version : '>=2.0.0', required : get_option('mplex'))
mplex2_dep = cxx.find_library('mplex2', required : get_option('mplex'))

if mjpegtools_dep.found() and mplex2_dep.found()
  gstmplex2 = library('gstmplex',
    'gstmplex.cc',
    'gstmplexibitstream.cc',
    'gstmplexjob.cc',
    'gstmplexoutputstream.cc',
    cpp_args : gst_plugins_bad_args + ['-DGST_MJPEGTOOLS_API=20000'],
    link_args : noseh_link_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstaudio_dep, mjpegtools_dep, mplex2_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstmplex2, install_dir : plugins_pkgconfig_install_dir)
endif
