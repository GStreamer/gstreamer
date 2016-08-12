stereo_sources = [
  'gststereo.c',
]

gststereo = library('gststereo',
  stereo_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstaudio_dep],
  install : true,
  install_dir : plugins_install_dir,
)
