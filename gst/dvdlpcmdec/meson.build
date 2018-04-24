dvdpl_sources = [
  'gstdvdlpcmdec.c'
]

gstdvdlpcmdec = library('gstdvdlpcmdec',
  dvdpl_sources,
  c_args : ugly_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstaudio_dep],
  install : true,
  install_dir : plugins_install_dir,
)
pkgconfig.generate(gstdvdlpcmdec, install_dir : plugins_pkgconfig_install_dir)
