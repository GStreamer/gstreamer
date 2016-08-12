insert_sources = ['gstinsertbin.c']
insert_headers = ['gstinsertbin.h']
install_headers(insert_headers, subdir : 'gstreamer-1.0/gst/insertbin')

gstinsertbin = library('gstinsertbin-' + api_version,
  insert_sources,
  c_args : gst_plugins_bad_args + [ '-DGST_USE_UNSTABLE_API' ],
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gst_dep],
  vs_module_defs: vs_module_defs_dir + 'libgstinsertbin.def',
)
# TODO: generate gir

gstinsertbin_dep = declare_dependency(link_with : gstinsertbin,
  include_directories : [libsinc],
  dependencies : [gst_dep])
