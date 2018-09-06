sctp_sources = [
  'gstsctpdec.c',
  'gstsctpenc.c',
  'gstsctpplugin.c',
  'sctpassociation.c'
]

if get_option('sctp').disabled()
  subdir_done()
endif

sctp_dep = cc.find_library('usrsctp', required : get_option('sctp').enabled())
sctp_header = cc.has_header('usrsctp.h')

if get_option('sctp').enabled()
  if not sctp_dep.found() or not sctp_header
    error('sctp plugin enabled but could not find libusrsctp')
  endif
endif

if sctp_dep.found() and sctp_header
  gstsctp = library('gstsctp',
    sctp_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [sctp_dep, gst_dep, gstbase_dep, gstsctp_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstsctp, install_dir : plugins_pkgconfig_install_dir)
endif
