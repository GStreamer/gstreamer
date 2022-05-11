webrtc_sources = [
  'gstwebrtc.c',
  'gstwebrtcice.c',
  'gstwebrtcstats.c',
  'icestream.c',
  'nicetransport.c',
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

libnice_dep = dependency('nice', version : '>=0.1.17', required : get_option('webrtc'),
                         fallback : ['libnice', 'libnice_dep'],
                         default_options: ['tests=disabled'])

if libnice_dep.found()
  libnice_version = libnice_dep.version()
  libnice_c_args = []
  if libnice_version.version_compare('<0.1.20') or libnice_version.version_compare('<0.1.19.1')
    version_arr = libnice_version.split('.')
    libnice_version_major = version_arr[0]
    libnice_version_minor = version_arr[1]
    libnice_version_micro = version_arr[2]
    if version_arr.length() == 4
      libnice_version_nano = version_arr[3]
    else
      libnice_version_nano = '0'
    endif
    libnice_c_args = ['-DNICE_VERSION_MAJOR=' + libnice_version_major,
                      '-DNICE_VERSION_MINOR=' + libnice_version_minor,
                      '-DNICE_VERSION_MICRO=' + libnice_version_micro,
                      '-DNICE_VERSION_NANO=' + libnice_version_nano ]
  endif
  gstwebrtc_plugin = library('gstwebrtc',
    webrtc_sources,
    c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'] + libnice_c_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep, gstsdp_dep,
                    gstapp_dep, gstwebrtc_dep, gstsctp_dep, gstrtp_dep, libnice_dep, gio_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstwebrtc_plugin, install_dir : plugins_pkgconfig_install_dir)
  plugins += [gstwebrtc_plugin]
endif
