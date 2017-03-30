if mjpegtools_dep.found()
  mplex2_dep = cxx.find_library('mplex2', required : false)

  if mplex2_dep.found()
    gstmplex2 = library('gstmplex',
      'gstmplex.cc',
      'gstmplexibitstream.cc',
      'gstmplexjob.cc',
      'gstmplexoutputstream.cc',
      cpp_args : gst_plugins_bad_args + ['-DGST_MJPEGTOOLS_API=' + mjpegtools_api],
      link_args : noseh_link_args,
      include_directories : [configinc, libsinc],
      dependencies : [gstaudio_dep, mjpegtools_dep, mplex2_dep],
      install : true,
      install_dir : plugins_install_dir,
    )
  endif
endif
