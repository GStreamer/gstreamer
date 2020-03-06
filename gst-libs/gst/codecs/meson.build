codecs_sources = files([
  'gsth264decoder.c',
  'gsth264picture.c',
  'gsth265decoder.c',
  'gsth265picture.c',
  'gstvp9decoder.c',
  'gstvp9picture.c',
])

codecs_headers = [
  'gsth264decoder.h',
  'gsth264picture.h',
  'gsth265decoder.h',
  'gsth265picture.h',
  'gstvp9decoder.h',
  'gstvp9picture.h',
]

cp_args = [
  '-DGST_USE_UNSTABLE_API',
  '-DBUILDING_GST_CODECS',
]

gstcodecs = library('gstcodecs-' + api_version,
  codecs_sources,
  c_args : gst_plugins_bad_args + cp_args,
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  darwin_versions : osxversion,
  install : true,
  dependencies : [gstvideo_dep, gstcodecparsers_dep],
)

gstcodecs_dep = declare_dependency(link_with : gstcodecs,
  include_directories : [libsinc],
  dependencies : [gstvideo_dep, gstcodecparsers_dep])
