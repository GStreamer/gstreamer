nuvd_sources = [
  'gstnuvdemux.c',
]

gstnuvdemux = library('gstnuvdemux',
  nuvd_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstbase_dep],
  install : true,
  install_dir : plugins_install_dir,
)
