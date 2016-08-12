gst_sdp_headers = [
  'sdp.h',
  'gstsdp.h',
  'gstmikey.h',
  'gstsdpmessage.h',
]
install_headers(gst_sdp_headers, subdir : 'gstreamer-1.0/gst/sdp/')

gstsdp = library('gstsdp-@0@'.format(api_version),
  'gstsdpmessage.c', 'gstmikey.c',
  c_args : gst_plugins_base_args,
  include_directories: [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [rtp_dep, gst_dep, gio_dep],
  vs_module_defs: vs_module_defs_dir + 'libgstsdp.def',
)
# TODO: generate gir

sdp_dep = declare_dependency(link_with: gstsdp,
  include_directories : [libsinc],
  dependencies : [rtp_dep, gst_dep, gio_dep])
