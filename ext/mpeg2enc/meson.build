if mjpegtools_dep.found()
  mpeg2enc_dep = cxx.find_library('mpeg2encpp', required : false)

  if mpeg2enc_dep.found()
    gstmpeg2enc = library('gstmpeg2enc',
      'gstmpeg2enc.cc',
      'gstmpeg2encoptions.cc',
      'gstmpeg2encoder.cc',
      'gstmpeg2encstreamwriter.cc',
      'gstmpeg2encpicturereader.cc',
      cpp_args : gst_plugins_bad_args + ['-DGST_MJPEGTOOLS_API=' + mjpegtools_api],
      link_args : noseh_link_args,
      include_directories : [configinc, libsinc],
      dependencies : [gstvideo_dep, mjpegtools_dep, mpeg2enc_dep],
      install : true,
      install_dir : plugins_install_dir,
    )
  endif
endif
