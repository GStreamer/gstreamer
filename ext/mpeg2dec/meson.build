mpeg2_dep = dependency('libmpeg2', version : '>= 0.4.0', required : get_option('mpeg2dec'))

if mpeg2_dep.found()
  mpeg2dec = library('gstmpeg2dec',
    ['gstmpeg2dec.c'],
    c_args : ugly_args,
    include_directories : [configinc],
    dependencies : [gstvideo_dep, mpeg2_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(mpeg2dec, install_dir : plugins_pkgconfig_install_dir)
endif
