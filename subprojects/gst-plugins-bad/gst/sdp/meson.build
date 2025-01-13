sdp_sources = [
  'gstsdpelem.c',
  'gstsdpdemux.h',
  'gstsdpdemux.c',
  'gstsdpsrc.c'
]

sdp_headers = [
  'gstsdpdemux.h',
  'gstsdpsrc.h',
]

doc_sources = []
foreach s: sdp_sources + sdp_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'sdpelem': pathsep.join(doc_sources)
}

if get_option('sdp').disabled()
  subdir_done()
endif

gstsdpelem = library('gstsdpelem',
  sdp_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstrtp_dep, gstsdp_dep, gio_dep, gstapp_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstsdpelem]
