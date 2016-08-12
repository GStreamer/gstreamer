directfb_dep = dependency('directfb', version : '>= 0.9.24', required : false)

if directfb_dep.found()
  gstdirectfb = library('gstdirectfb',
    'dfbvideosink.c',
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstvideo_dep, directfb_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
