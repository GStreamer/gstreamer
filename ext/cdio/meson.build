cdio_dep = dependency('libcdio', version : '>= 0.76', required : get_option('cdio'))

if cdio_dep.found()
  cdio = library('gstcdio',
    ['gstcdio.c', 'gstcdiocddasrc.c'],
    c_args : ugly_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstaudio_dep, gsttag_dep, cdio_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(cdio, install_dir : plugins_pkgconfig_install_dir)
endif
