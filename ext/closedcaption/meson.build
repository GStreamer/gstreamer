pangocairo_dep = dependency('pangocairo', version : '>= 1.22.0', required : false)

if pangocairo_dep.found()
  gstclosedcaption = library('gstclosedcaption',
    'gstccextractor.c', 'gstclosedcaption.c',
    c_args : gst_plugins_bad_args,
    link_args : noseh_link_args,
    include_directories : [configinc],
    dependencies : [gstvideo_dep, pangocairo_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstclosedcaption, install_dir : plugins_pkgconfig_install_dir)
endif
