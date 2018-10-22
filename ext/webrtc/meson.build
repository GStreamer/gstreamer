webrtc_sources = [
  'gstwebrtc.c',
  'gstwebrtcice.c',
  'gstwebrtcstats.c',
  'icestream.c',
  'nicetransport.c',
  'sctptransport.c',
  'gstwebrtcbin.c',
  'transportreceivebin.c',
  'transportsendbin.c',
  'transportstream.c',
  'utils.c',
  'webrtcsdp.c',
  'webrtctransceiver.c',
  'webrtcdatachannel.c',
]

libnice_dep = dependency('nice', version : '>=0.1.14', required : get_option('webrtc'),
                         fallback : ['libnice', 'libnice_dep'],
                         default_options: ['tests=disabled'])

if libnice_dep.found()
  gstwebrtc_plugin = library('gstwebrtc',
    webrtc_sources,
    c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
    include_directories : [configinc],
    dependencies : [gio_dep, libnice_dep, gstbase_dep, gstsdp_dep,
                    gstapp_dep, gstwebrtc_dep, gstsctp_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstwebrtc_plugin, install_dir : plugins_pkgconfig_install_dir)
  plugins += [gstwebrtc_plugin]
endif
