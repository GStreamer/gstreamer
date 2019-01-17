avtp_sources = [
  'gstavtp.c',
  'gstavtpaafpay.c',
  'gstavtpbasepayload.c',
]

avtp_dep = dependency('avtp', required: get_option('avtp'))

if avtp_dep.found()
  gstavtp = library('gstavtp',
    avtp_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstaudio_dep, avtp_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstavtp, install_dir : plugins_pkgconfig_install_dir)
endif
