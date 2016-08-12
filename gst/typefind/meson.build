gsttypefind = library('gsttypefindfunctions',
  'gsttypefindfunctions.c',
  c_args : gst_plugins_base_args,
  include_directories: [configinc, libsinc],
  dependencies : [pbutils_dep, gst_base_dep],
  install : true,
  install_dir : plugins_install_dir,
)
