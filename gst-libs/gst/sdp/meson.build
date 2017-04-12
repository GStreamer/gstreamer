gst_sdp_headers = [
  'sdp.h',
  'gstsdp.h',
  'gstmikey.h',
  'gstsdpmessage.h',
]
install_headers(gst_sdp_headers, subdir : 'gstreamer-1.0/gst/sdp/')

rtsp_deps = [rtp_dep, gst_dep, gio_dep]
gst_sdp_sources = ['gstsdpmessage.c', 'gstmikey.c']
gstsdp = library('gstsdp-@0@'.format(api_version),
  gst_sdp_sources,
  c_args : gst_plugins_base_args,
  include_directories: [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : rtsp_deps,
  vs_module_defs: vs_module_defs_dir + 'libgstsdp.def',
)

sdp_gen_sources  = []
if build_gir
  gst_gir_extra_args = gir_init_section + [ '--c-include=gst/sdp/sdp.h' ]
  sdp_gen_sources += [gnome.generate_gir(gstsdp,
    sources : gst_sdp_sources + gst_sdp_headers,
    namespace : 'GstSdp',
    nsversion : api_version,
    identifier_prefix : 'Gst',
    symbol_prefix : 'gst',
    export_packages : 'gstreamer-sdp-1.0',
    includes : ['Gst-1.0'],
    install : true,
    extra_args : gst_gir_extra_args,
    dependencies : rtsp_deps
  )]
endif

sdp_dep = declare_dependency(link_with: gstsdp,
  include_directories : [libsinc],
  dependencies : rtsp_deps,
  sources: sdp_gen_sources)
