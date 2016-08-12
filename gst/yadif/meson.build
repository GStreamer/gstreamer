yadif_sources = [
  'gstyadif.c',
  'vf_yadif.c',
  'yadif.c'
]

gstyadif = library('gstyadif',
  yadif_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep],
  install : true,
  install_dir : plugins_install_dir,
)
