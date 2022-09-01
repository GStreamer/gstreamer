dvdread_opt = get_option('dvdread').require(gpl_allowed, error_message: '''
  Plugin dvdread explicitly required via options but GPL-licensed plugins disabled via options.
  Pass option -Dgpl=enabled to Meson to allow GPL-licensed plugins to be built.
  ''')

dvdread_dep = dependency('dvdread', version : '>= 0.5.0', required : dvdread_opt)

if dvdread_dep.found()
  dvdread = library('gstdvdread',
    ['dvdreadsrc.c'],
    c_args : ugly_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstbase_dep, gmodule_dep, dvdread_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [dvdread]
endif
