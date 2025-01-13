cdio_opt = get_option('cdio').require(gpl_allowed, error_message: '''
  Plugin cdio explicitly required via options but GPL-licensed plugins disabled via options.
  Pass option -Dgpl=enabled to Meson to allow GPL-licensed plugins to be built.
  ''')

cdio_sources = [
  'gstcdio.c',
  'gstcdiocddasrc.c',
]

cdio_headers = [
  'gstcdiocddasrc.h',
  'gstcdio.h',
]

doc_sources = []
foreach s: cdio_sources + cdio_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'cdio': pathsep.join(doc_sources)
}

cdio_dep = dependency('libcdio', version : '>= 0.76', required : cdio_opt)

if cdio_dep.found()

  cdio = library('gstcdio',
    cdio_sources,
    c_args : ugly_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstaudio_dep, gsttag_dep, cdio_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [cdio]
endif
