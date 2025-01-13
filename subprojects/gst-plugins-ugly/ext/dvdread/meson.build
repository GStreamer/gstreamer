dvdread_opt = get_option('dvdread').require(gpl_allowed, error_message: '''
  Plugin dvdread explicitly required via options but GPL-licensed plugins disabled via options.
  Pass option -Dgpl=enabled to Meson to allow GPL-licensed plugins to be built.
  ''')

dvdread_sources = [
  'dvdreadsrc.c',
]

dvdread_headers = [
  'dvdreadsrc.h',
]

doc_sources = []
foreach s: dvdread_sources + dvdread_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'dvdread': pathsep.join(doc_sources)
}

dvdread_dep = dependency('dvdread', version : '>= 0.5.0', required : dvdread_opt)

if dvdread_dep.found()
  dvdread = library('gstdvdread',
    dvdread_sources,
    c_args : ugly_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstbase_dep, gmodule_dep, dvdread_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [dvdread]
endif
