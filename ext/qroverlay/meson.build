qrencode_dep = dependency('libqrencode', required: get_option('qroverlay'))
if qrencode_dep.found()
  gstqroverlay = library('gstqroverlay', ['gstqroverlay.c'],
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep, qrencode_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstqroverlay, install_dir : plugins_pkgconfig_install_dir)
  plugins += [gstqroverlay]
endif

