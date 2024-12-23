srtp_sources = [
  'gstsrtp.c',
  'gstsrtpelement.c',
  'gstsrtpplugin.c',
  'gstsrtpdec.c',
  'gstsrtpenc.c',
]

srtp_headers = [
  'gstsrtp.h',
  'gstsrtpenums.h',
  'gstsrtpenc.h',
  'gstsrtpdec.h',
  'gstsrtpelements.h',
]

doc_sources = []
foreach s: srtp_sources + srtp_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'srtp': pathsep.join(doc_sources)
}

srtp_cargs = []
srtp_option = get_option('srtp').enable_if(get_option('webrtc').enabled(), error_message: 'webrtc option is enabled')
if srtp_option.disabled()
  srtp_dep = dependency('', required : false)
  subdir_done()
endif

srtp_dep = dependency('libsrtp2', version : '>= 2.1.0', required : srtp_option, allow_fallback: true)

if srtp_dep.found()
  gstsrtp_enums = gnome.mkenums_simple('gstsrtp-enumtypes',
    sources : ['gstsrtpenums.h'],
    decorator : 'G_GNUC_INTERNAL',
    install_header: false)

  gstsrtp = library('gstsrtp',
    srtp_sources, gstsrtp_enums,
    c_args : gst_plugins_bad_args + srtp_cargs,
    link_args : noseh_link_args,
    include_directories : [configinc],
    dependencies : [gstrtp_dep, gstvideo_dep, srtp_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstsrtp]
endif
