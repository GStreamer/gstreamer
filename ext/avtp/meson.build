avtp_sources = [
  'gstavtp.c',
  'gstavtpaafdepay.c',
  'gstavtpaafpay.c',
  'gstavtpcvfdepay.c',
  'gstavtpcvfpay.c',
  'gstavtpbasedepayload.c',
  'gstavtpbasepayload.c',
  'gstavtpsink.c',
  'gstavtpsrc.c',
  'gstavtpcrfutil.c',
  'gstavtpcrfbase.c',
  'gstavtpcrfsync.c',
  'gstavtpcrfcheck.c',
]

avtp_dep = dependency('avtp', required: get_option('avtp'),
                      fallback: ['avtp', 'avtp_dep'])

if avtp_dep.found() and cc.has_type('struct sock_txtime', prefix : '#include <linux/net_tstamp.h>')
  gstavtp = library('gstavtp',
    avtp_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstaudio_dep, gstvideo_dep, avtp_dep, libm],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstavtp, install_dir : plugins_pkgconfig_install_dir)
  plugins += [gstavtp]
endif
