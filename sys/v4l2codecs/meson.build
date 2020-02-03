v4l2codecs_sources = [
  'plugin.c',
]

gstv4l2codecs = library('gstv4l2codecs',
  v4l2codecs_sources,
  c_args : gst_plugins_bad_args,
  cpp_args: gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstcodecs_dep],
  install : true,
  install_dir : plugins_install_dir,
)
pkgconfig.generate(gstv4l2codecs, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstv4l2codecs]
