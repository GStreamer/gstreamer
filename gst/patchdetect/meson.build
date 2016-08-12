patch_sources = [
  'gstpatchdetect.c',
]

gstpatchdetect = library('gstpatchdetect',
  patch_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
