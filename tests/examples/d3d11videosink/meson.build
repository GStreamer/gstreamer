if host_system == 'windows'
  executable('d3d11videosink',
    ['d3d11videosink.c', 'd3d11videosink-kb.c'],
    c_args : gst_plugins_bad_args,
    include_directories : [configinc, libsinc],
    dependencies: [gst_dep, gstbase_dep, gstvideo_dep],
    install: false,
  )
endif
