real_sources = [
  'rademux.c',
  'rmdemux.c',
  'rmutils.c',
  'rdtdepay.c',
  'rdtmanager.c',
  'rtspreal.c',
  'realhash.c',
  'asmrules.c',
  'rdtjitterbuffer.c',
  'gstrdtbuffer.c',
  'pnmsrc.c',
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
pkgconfig.generate(gstrmdemux, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstrmdemux]
