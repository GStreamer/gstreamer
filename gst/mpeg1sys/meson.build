mpeg1_sources = [
  'gstmpeg1systemencode.c',
  'buffer.c',
  'systems.c',
]

gstmpeg1systemencode = library('gstmpeg1systemencode',
  mpeg1_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep],
  install : true,
  install_dir : plugins_install_dir,
)
