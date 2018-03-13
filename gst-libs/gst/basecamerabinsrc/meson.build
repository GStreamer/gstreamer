camerabin_sources = [
  'gstcamerabin-enum.c',
  'gstcamerabinpreview.c',
  'gstbasecamerasrc.c',
]
camerabin_headers = [
  'basecamerabinsrc-prelude.h',
  'gstcamerabin-enum.h',
  'gstcamerabinpreview.h',
  'gstbasecamerasrc.h',
]
install_headers(camerabin_headers, subdir : 'gstreamer-1.0/gst/basecamerabinsrc')

gstbasecamerabin = library('gstbasecamerabinsrc-' + api_version,
  camerabin_sources,
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gstapp_dep],
)

gstbasecamerabin_dep = declare_dependency(link_with : gstbasecamerabin,
  include_directories : [libsinc],
  dependencies : [gstapp_dep])
