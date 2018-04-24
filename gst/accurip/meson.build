accurip_sources = [
  'gstaccurip.c',
]

gstaccurip = library('gstaccurip',
  accurip_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstaudio_dep],
  install : true,
  install_dir : plugins_install_dir,
)
pkgconfig.generate(gstaccurip, install_dir : plugins_pkgconfig_install_dir)
