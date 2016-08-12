vbi_sources = [
  'gstvbidec.c',
  'vbidata.c',
  'vbiscreen.c',
]

gstvbidec = library('gstvbidec',
  vbi_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep],
  install : true,
  install_dir : plugins_install_dir,
)
