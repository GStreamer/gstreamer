gmodule_dep = dependency('gmodule-2.0', required : false)
dvdread_dep = dependency('dvdread', version : '>= 0.5.0', required : false)

if gmodule_dep.found() and dvdread_dep.found()
  dvdread = library('gstdvdread',
    ['dvdreadsrc.c'],
    c_args : ugly_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstbase_dep, gmodule_dep, dvdread_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
