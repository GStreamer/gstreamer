gstximage = library('gstximagesink',
  'ximagesink.c', 'ximage.c', 'ximagepool.c',
  c_args : gst_plugins_base_args,
  include_directories: [configinc, libsinc],
  dependencies : glib_deps + [video_dep, gst_base_dep, gst_dep, x11_dep, xshm_dep],
  install : true,
  install_dir : plugins_install_dir,
)
