gstcutter = library('gstcutter', 'gstcutter.c',
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstaudio_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
