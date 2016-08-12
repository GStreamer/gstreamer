vconvert_sources = [
  'gstvideoconvert.c',
]

gstvideoconvert = library('gstvideoconvert',
  vconvert_sources,
  c_args : gst_plugins_base_args,
  include_directories: [configinc, libsinc],
  dependencies : [video_dep],
  install : true,
  install_dir : plugins_install_dir,
)
