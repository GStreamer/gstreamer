rtjpeg_sources = [
  'gstrtjpeg.c',
  'gstrtjpegenc.c',
  'gstrtjpegdec.c',
  'RTjpeg.c',
]

gstrtjpeg = library('gstrtjpeg',
  rtjpeg_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep],
  install : true,
  install_dir : plugins_install_dir,
)
