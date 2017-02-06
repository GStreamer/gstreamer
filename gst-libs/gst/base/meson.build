gstvaapi_baseutils_sources = [
  'gstbitwriter.c',
]

gstvaapi_baseutils_headers = [
  'gstbitwriter.h',
]

gstvaapi_baseutils_deps = [gstbase_dep]

gstvaapi_baseutils = static_library('gstvaapi-baseutils-@0@'.format(api_version),
  gstvaapi_baseutils_sources,
  c_args : gstreamer_vaapi_args,
  include_directories: [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  dependencies : gstvaapi_baseutils_deps,
)

gstvaapi_baseutils_dep = declare_dependency(link_with: gstvaapi_baseutils,
  include_directories : [libsinc],
  dependencies : gstvaapi_baseutils_deps)
