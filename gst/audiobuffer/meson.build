audiobuffer_sources = [
  'gstaudioringbuffer.c',
]

gstaudiobuffer = library('gstaudiobuffer',
  audiobuffer_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstbase_dep, gstaudio_dep],
  install : true,
  install_dir : plugins_install_dir,
)
