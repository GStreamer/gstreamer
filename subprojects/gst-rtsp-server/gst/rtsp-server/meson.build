rtsp_server_sources = files(
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
)

rtsp_server_headers = files(
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
)

install_headers(rtsp_server_headers, subdir : 'gstreamer-1.0/gst/rtsp-server')

gst_rtsp_server_deps = [gstrtsp_dep, gstrtp_dep, gstsdp_dep, gstnet_dep, gstapp_dep, gst_dep, gstvideo_dep]
gst_rtsp_server = library('gstrtspserver-@0@'.format(api_version),
  rtsp_server_sources,
  include_directories : rtspserver_incs,
  c_args: rtspserver_args + ['-DBUILDING_GST_RTSP_SERVER', '-DG_LOG_DOMAIN="GStreamer-RTSP-Server"'],
  version : libversion,
  soversion : soversion,
  darwin_versions : osxversion,
  install : true,
  dependencies : gst_rtsp_server_deps)

library_def = {'lib': gst_rtsp_server}
pkg_name = 'gstreamer-rtsp-server-' + api_version
pkgconfig.generate(gst_rtsp_server,
  libraries : [gst_dep],
  subdirs : pkgconfig_subdirs,
  name : 'gstreamer-rtsp-server-1.0',
  description : 'GStreamer based RTSP server',
)

rtsp_server_gen_sources = []
if build_gir
  gst_gir_extra_args = gir_init_section + ['--c-include=gst/rtsp-server/rtsp-server.h']
  gir = {
    'sources' : rtsp_server_headers + rtsp_server_sources,
    'namespace' : 'GstRtspServer',
    'nsversion' : api_version,
    'identifier_prefix' : 'Gst',
    'symbol_prefix' : 'gst',
    'export_packages' : pkg_name,
    'install' : true,
    'extra_args' : gst_gir_extra_args,
    'includes' : ['Gst-1.0', 'GstRtsp-1.0', 'GstNet-1.0'],
    'dependencies' : gst_rtsp_server_deps,
  }
  # FIXME: Expose gir for use in gstreamer-full
  # Disabled for now as we get `undefined reference` to many symbols
  # library_def = {'gir': [gir]}
  if not static_build
    rtsp_server_gir = gnome.generate_gir(gst_rtsp_server, kwargs: gir)
    library_def += {'gir_targets':  library_def.get('gir_targets', []) + [rtsp_server_gir]}
    rtsp_server_gen_sources += [rtsp_server_gir]
  endif
endif
gst_libraries += [[pkg_name, library_def]]

gst_rtsp_server_dep = declare_dependency(link_with : gst_rtsp_server,
  include_directories : rtspserver_incs,
  sources : rtsp_server_gen_sources,
  dependencies : [gstrtsp_dep, gstrtp_dep, gstsdp_dep, gstnet_dep, gstapp_dep, gstvideo_dep])

meson.override_dependency(pkg_name, gst_rtsp_server_dep)
