gstalpha = library('gstalpha', 'gstalpha.c',
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gstvideo_dep, gst_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)

gstalphacolor = library('gstalphacolor', 'gstalphacolor.c',
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gstvideo_dep, gst_dep],
  install : true,
  install_dir : plugins_install_dir,
)
