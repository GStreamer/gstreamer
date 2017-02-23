timecode_sources = [
  'plugin.c',
  'gsttimecodestamper.c',
  'gstavwait.c'
]

gsttimecode = library('gsttimecode',
  timecode_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstaudio_dep, gstvideo_dep],
  install : true,
  install_dir : plugins_install_dir,
)
