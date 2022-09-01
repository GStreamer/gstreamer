gstshapewipe = library('gstshapewipe',
  'gstshapewipe.c',
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gst_dep, gstvideo_dep, gio_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstshapewipe]
