webrtc_sources = [
  'gstwebrtc.c',
  'gstwebrtcstats.c',
  'webrtcsctptransport.c',
  'gstwebrtcbin.c',
  'transportreceivebin.c',
  'transportsendbin.c',
  'transportstream.c',
  'utils.c',
  'webrtcsdp.c',
  'webrtctransceiver.c',
  'webrtcdatachannel.c',
]

webrtc_headers = [
  'transportstream.h',
  'gstwebrtcbin.h',
  'transportreceivebin.h',
  'fwd.h',
  'transportsendbin.h',
  'webrtcdatachannel.h',
  'webrtcsctptransport.h',
  'webrtctransceiver.h',
  'utils.h',
  'webrtcsdp.h',
  'gstwebrtcstats.h',
]

doc_sources = []
foreach s: webrtc_sources + webrtc_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'webrtc': pathsep.join(doc_sources)
}

webrtc_option = get_option('webrtc').require(
  libgstwebrtcnice_dep.found(), error_message: 'webrtc plugin requires libgstwebrtcnice.')
if webrtc_option.disabled()
  subdir_done()
endif

gstwebrtc_plugin = library('gstwebrtc',
  webrtc_sources,
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstsdp_dep,
                  gstapp_dep, gstwebrtc_dep, gstsctp_dep, gstrtp_dep, gio_dep, libgstwebrtcnice_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstwebrtc_plugin]
