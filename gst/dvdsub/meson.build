dvdsub_sources = [
  'gstdvdsubdec.c',
  'gstdvdsubparse.c',
]

gstdvdsub = library('gstdvdsub',
  dvdsub_sources,
  c_args : ugly_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep],
  install : true,
  install_dir : plugins_install_dir,
)
