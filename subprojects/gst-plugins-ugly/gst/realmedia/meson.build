real_sources = [
  'rademux.c',
  'rmdemux.c',
  'rmutils.c',
  'realmedia.c'
]

gstrmdemux = library('gstrealmedia',
  real_sources,
  c_args : ugly_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstbase_dep, gstrtsp_dep, gstsdp_dep, gstpbutils_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstrmdemux]
