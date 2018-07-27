srtp_sources = [
  'gstsrtp.c',
  'gstsrtpdec.c',
  'gstsrtpenc.c',
]

srtp_cargs = []
if get_option('srtp').disabled()
  subdir_done()
endif

srtp_dep = dependency('libsrtp2', version : '>= 2.1.0', required : false)
if srtp_dep.found()
  srtp_cargs += ['-DHAVE_SRTP2']
else
  srtp_dep = dependency('libsrtp', required : false)
  if not srtp_dep.found() and cc.has_header_symbol('srtp/srtp.h', 'srtp_init')
    srtp_dep = cc.find_library('srtp', required : false)
  endif
endif
if not srtp_dep.found() and get_option('srtp').enabled()
  error('srtp plugin enabled but libsrtp not found')
endif

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
  pkgconfig.generate(gstsrtp, install_dir : plugins_pkgconfig_install_dir)
endif
