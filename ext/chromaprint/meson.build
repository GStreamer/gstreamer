chromaprint_dep = dependency('libchromaprint', required : false)

if chromaprint_dep.found()
  gstchromaprint = library('gstchromaprint',
    'gstchromaprint.c',
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstaudio_dep, chromaprint_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
