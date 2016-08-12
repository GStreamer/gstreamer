raw_sources = [
  'gstunalignedaudioparse.c',
  'gstunalignedvideoparse.c',
  'gstaudioparse.c',
  'gstvideoparse.c',
  'gstrawbaseparse.c',
  'gstrawaudioparse.c',
  'gstrawvideoparse.c',
  'plugin.c',
]

gstrawparse = library('gstrawparse',
  raw_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep, gstaudio_dep],
  install : true,
  install_dir : plugins_install_dir,
)
