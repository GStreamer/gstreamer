badallocators_sources = [
  'gstphysmemory.c',
]

badallocators_headers = [
  'badallocators.h',
  'gstphysmemory.h',
]

install_headers(badallocators_headers, subdir : 'gstreamer-1.0/gst/allocators')

gstbadallocators = library('gstbadallocators-' + api_version,
  badallocators_sources,
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gst_dep]
)

gstbadallocators_dep = declare_dependency(link_with : gstbadallocators,
  include_directories : [libsinc],
  dependencies : [gst_dep])
