adder = library('gstapp',
  'gstapp.c',
  c_args: gst_plugins_base_args,
  include_directories: [configinc],
  dependencies : [gst_base_dep, app_dep, tag_dep],
  install : true,
  install_dir : plugins_install_dir,
)
