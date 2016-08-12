x265_dep = dependency('x265', required: false)
if x265_dep.found()
  gstx265 = library('gstx265',
    'gstx265enc.c',
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstpbutils_dep, gstvideo_dep, x265_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
