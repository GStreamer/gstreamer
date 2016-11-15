gstwayland = library('gstwayland-' + api_version,
  'wayland.c',
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gst_dep, gstvideo_dep]
)

gstwayland_dep = declare_dependency(link_with : gstwayland,
  include_directories : [libsinc],
  dependencies : [gst_dep, gstvideo_dep])
