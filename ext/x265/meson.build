x265_dep = dependency('x265', required: get_option('x265'))
if x265_dep.found()
  gstx265 = library('gstx265',
    'gstx265enc.c',
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstpbutils_dep, gstvideo_dep, x265_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstx265, install_dir : plugins_pkgconfig_install_dir)
endif
