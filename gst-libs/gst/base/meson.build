badbase_sources = ['gstaggregator.c']
badbase_headers = ['gstaggregator.h']
install_headers(badbase_headers, subdir : 'gstreamer-1.0/gst/base')

gstbadbase = library('gstbadbase-' + api_version,
  badbase_sources,
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gst_dep],
  vs_module_defs: vs_module_defs_dir + 'libgstbadbase.def',
)

gstbadbase_dep = declare_dependency(link_with : gstbadbase,
  include_directories : [libsinc],
  dependencies : [gst_dep])
