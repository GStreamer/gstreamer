rtsp_server_sources = [
  'rtsp-address-pool.c',
  'rtsp-auth.c',
  'rtsp-client.c',
  'rtsp-context.c',
  'rtsp-latency-bin.c',
  'rtsp-media.c',
  'rtsp-media-factory.c',
  'rtsp-media-factory-uri.c',
  'rtsp-mount-points.c',
  'rtsp-params.c',
  'rtsp-permissions.c',
  'rtsp-sdp.c',
  'rtsp-server.c',
  'rtsp-session.c',
  'rtsp-session-media.c',
  'rtsp-session-pool.c',
  'rtsp-stream.c',
  'rtsp-stream-transport.c',
  'rtsp-thread-pool.c',
  'rtsp-token.c',
  'rtsp-onvif-server.c',
  'rtsp-onvif-client.c',
  'rtsp-onvif-media-factory.c',
  'rtsp-onvif-media.c',
]

rtsp_server_headers = [
  'rtsp-auth.h',
  'rtsp-address-pool.h',
  'rtsp-context.h',
  'rtsp-params.h',
  'rtsp-sdp.h',
  'rtsp-thread-pool.h',
  'rtsp-media.h',
  'rtsp-media-factory.h',
  'rtsp-media-factory-uri.h',
  'rtsp-mount-points.h',
  'rtsp-permissions.h',
  'rtsp-stream.h',
  'rtsp-stream-transport.h',
  'rtsp-session.h',
  'rtsp-session-media.h',
  'rtsp-session-pool.h',
  'rtsp-token.h',
  'rtsp-client.h',
  'rtsp-server.h',
  'rtsp-server-object.h',
  'rtsp-server-prelude.h',
  'rtsp-onvif-server.h',
  'rtsp-onvif-client.h',
  'rtsp-onvif-media-factory.h',
  'rtsp-onvif-media.h',
]

install_headers(rtsp_server_headers, subdir : 'gstreamer-1.0/gst/rtsp-server')

gst_rtsp_server_deps = [gstrtsp_dep, gstrtp_dep, gstsdp_dep, gstnet_dep, gstapp_dep]
gst_rtsp_server = library('gstrtspserver-@0@'.format(api_version),
  rtsp_server_sources,
  include_directories : rtspserver_incs,
  c_args: rtspserver_args + ['-DBUILDING_GST_RTSP_SERVER'],
  version : libversion,
  soversion : soversion,
  darwin_versions : osxversion,
  install : true,
  dependencies : gst_rtsp_server_deps)

rtsp_server_gen_sources = []
if build_gir
  gst_gir_extra_args = gir_init_section + ['--c-include=gst/rtsp-server/rtsp-server.h']
  rtsp_server_gir = gnome.generate_gir(gst_rtsp_server,
    sources : rtsp_server_headers + rtsp_server_sources,
    namespace : 'GstRtspServer',
    nsversion : api_version,
    identifier_prefix : 'Gst',
    symbol_prefix : 'gst',
    export_packages : 'gstreamer-rtsp-server-' + api_version,
    install : true,
    extra_args : gst_gir_extra_args,
    includes : ['Gst-1.0', 'GstRtsp-1.0', 'GstNet-1.0'],
    dependencies : gst_rtsp_server_deps,
  )
  rtsp_server_gen_sources += [rtsp_server_gir]
endif

gst_rtsp_server_dep = declare_dependency(link_with : gst_rtsp_server,
  include_directories : rtspserver_incs,
  sources : rtsp_server_gen_sources,
  dependencies : [gstrtsp_dep, gstrtp_dep, gstsdp_dep, gstnet_dep, gstapp_dep])
