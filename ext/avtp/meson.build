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
]

avtp_dep = dependency('avtp', required: get_option('avtp'))

if avtp_dep.found()
  gstavtp = library('gstavtp',
    avtp_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstaudio_dep, gstvideo_dep, avtp_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstavtp, install_dir : plugins_pkgconfig_install_dir)
  plugins += [gstavtp]
endif
