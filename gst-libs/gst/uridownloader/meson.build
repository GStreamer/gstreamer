urid_sources = [
  'gstfragment.c',
  'gsturidownloader.c',
]
urid_headers = [
  'gstfragment.h',
  'gsturidownloader.h',
  'gsturidownloader_debug.h',
]
install_headers(urid_headers, subdir : 'gstreamer-1.0/gst/uridownloader')

gsturidownloader = library('gsturidownloader-' + api_version,
  urid_sources,
  c_args : gst_plugins_bad_args + [ '-DGST_USE_UNSTABLE_API' ],
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gstbase_dep],
  vs_module_defs: vs_module_defs_dir + 'libgsturidownloader.def',
)

gsturidownloader_dep = declare_dependency(link_with : gsturidownloader,
  include_directories : [libsinc],
  dependencies : [gstbase_dep])
