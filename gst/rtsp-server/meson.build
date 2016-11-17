rtsp_server_sources = [
  'rtsp-address-pool.c',
  'rtsp-auth.c',
  'rtsp-client.c',
  'rtsp-context.c',
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
]
install_headers(rtsp_server_headers, subdir : 'gstreamer-1.0/gst/rtsp-server')

gst_rtsp_server = library('gstrtspserver-@0@'.format(api_version),
  rtsp_server_sources,
  include_directories : rtspserver_incs,
  c_args: rtspserver_args,
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gstrtsp_dep, gstrtp_dep, gstsdp_dep, gstnet_dep, gstapp_dep],
  vs_module_defs: vs_module_defs_dir + 'libgstrtspserver.def'
)

gst_rtsp_server_dep = declare_dependency(link_with : gst_rtsp_server,
  include_directories : rtspserver_incs,
  dependencies : [gstrtsp_dep, gstrtp_dep, gstsdp_dep, gstnet_dep, gstapp_dep])
