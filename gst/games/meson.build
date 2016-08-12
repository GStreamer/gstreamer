games_sources = [
  'gstvideoimage.c',
  'gstpuzzle.c',
]

gstpuzzle = library('gstpuzzle',
  games_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep],
  install : true,
  install_dir : plugins_install_dir,
)
