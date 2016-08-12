hdv_sources = [
  'gsthdvparse.c'
]

gsthdvparse = library('gsthdvparse',
  hdv_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
