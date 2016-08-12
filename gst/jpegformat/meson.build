jpegf_sources = [
  'gstjpegformat.c',
  'gstjpegparse.c',
  'gstjifmux.c',
]

gstjpegformat = library('gstjpegformat',
  jpegf_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gsttag_dep],
  install : true,
  install_dir : plugins_install_dir,
)
