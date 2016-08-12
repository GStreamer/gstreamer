badvideo_sources = [
  'gstvideoaggregator.c',
]
badvideo_headers = [
  'gstvideoaggregatorpad.h',
  'gstvideoaggregator.h'
]
install_headers(badvideo_headers, subdir : 'gstreamer-1.0/gst/video')


gstbadvideo = library('gstbadvideo-' + api_version,
  badvideo_sources,
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gstvideo_dep, gstbadbase_dep],
  vs_module_defs: vs_module_defs_dir + 'libgstbadvideo.def',
)

gstbadvideo_dep = declare_dependency(link_with : gstbadvideo,
  include_directories : [libsinc],
  dependencies : [gstvideo_dep, gstbadbase_dep])
