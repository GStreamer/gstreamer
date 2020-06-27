gst_sdp_headers = files([
  'sdp.h',
  'sdp-prelude.h',
  'gstsdp.h',
  'gstmikey.h',
  'gstsdpmessage.h',
])
install_headers(gst_sdp_headers, subdir : 'gstreamer-1.0/gst/sdp/')

sdp_deps = [rtp_dep, gst_dep, gio_dep]
gst_sdp_sources = files(['gstsdpmessage.c', 'gstmikey.c'])
gstsdp = library('gstsdp-@0@'.format(api_version),
  gst_sdp_sources,
  c_args : gst_plugins_base_args + ['-DBUILDING_GST_SDP'],
  include_directories: [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  darwin_versions : osxversion,
  install : true,
  dependencies : sdp_deps,
)

pkgconfig.generate(gstsdp,
  libraries : [glib_deps],
  variables : pkgconfig_variables,
  subdirs : pkgconfig_subdirs,
  name : 'gstreamer-sdp-1.0',
  description : 'SDP helper functions',
)

sdp_gen_sources  = []
if build_gir
  gst_gir_extra_args = gir_init_section + [ '--c-include=gst/sdp/sdp.h' ]
  sdp_gir = gnome.generate_gir(gstsdp,
    sources : gst_sdp_sources + gst_sdp_headers,
    namespace : 'GstSdp',
    nsversion : api_version,
    identifier_prefix : 'Gst',
    symbol_prefix : 'gst',
    export_packages : 'gstreamer-sdp-1.0',
    includes : ['Gst-1.0'],
    install : true,
    extra_args : gst_gir_extra_args,
    dependencies : sdp_deps
  )

  sdp_gen_sources += [sdp_gir]
endif

sdp_dep = declare_dependency(link_with: gstsdp,
  include_directories : [libsinc],
  dependencies : sdp_deps,
  sources: sdp_gen_sources)

meson.override_dependency('gstreamer-sdp-1.0', sdp_dep)
