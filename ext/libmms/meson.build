mms_dep = dependency('libmms', version : '>= 0.4', required : get_option('libmms'))

if mms_dep.found()
  gstmms = library('gstmms',
    'gstmms.c',
    c_args : gst_plugins_bad_args,
    link_args : noseh_link_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep, mms_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstmms, install_dir : plugins_pkgconfig_install_dir)
endif
