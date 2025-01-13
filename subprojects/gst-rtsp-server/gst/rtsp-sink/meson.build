rtspsink_sources = [
  'gstrtspclientsink.c',
  'plugin.c',
]

rtspsink_headers = [
  'gstrtspclientsink.h',
]

doc_sources = []
foreach s: rtspsink_sources + rtspsink_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'rtspclientsink': pathsep.join(doc_sources)
}

if get_option('rtspclientsink').disabled()
  subdir_done()
endif

rtspsink = library('gstrtspclientsink',
  rtspsink_sources,
  c_args : rtspserver_args + ['-DG_LOG_DOMAIN="GStreamer-rtspclientsink"'],
  include_directories : rtspserver_incs,
  dependencies : [gstrtsp_dep, gstsdp_dep, gst_rtsp_server_dep],
  install : true,
  install_dir : plugins_install_dir)
plugins += [rtspsink]
