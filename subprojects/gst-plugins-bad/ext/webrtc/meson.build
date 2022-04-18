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

gstwebrtc_plugin = library('gstwebrtc',
  webrtc_sources,
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstsdp_dep,
                  gstapp_dep, gstwebrtc_dep, gstsctp_dep, gstrtp_dep, gio_dep, libgstwebrtcnice_dep],
  install : true,
  install_dir : plugins_install_dir,
)
pkgconfig.generate(gstwebrtc_plugin, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstwebrtc_plugin]

