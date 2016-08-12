overlay_sources = [
  'gstoverlay.c',
]

gstoverlay = library('gstoverlay',
  overlay_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gst_dep],
  install : true,
  install_dir : plugins_install_dir,
)
