mpeg2dec_opt = get_option('mpeg2dec').require(gpl_allowed, error_message: '''
  Plugin mpeg2dec explicitly required via options but GPL-licensed plugins disabled via options.
  Pass option -Dgpl=enabled to Meson to allow GPL-licensed plugins to be built.
  ''')

mpeg2dec_sources = [
  'gstmpeg2dec.c',
]

mpeg2dec_headers = [
  'gstmpeg2dec.h',
]

doc_sources = []
foreach s: mpeg2dec_sources + mpeg2dec_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'mpeg2dec': pathsep.join(doc_sources)
}

mpeg2_dep = dependency('libmpeg2', version : '>= 0.4.0', required : mpeg2dec_opt)

if mpeg2_dep.found()
  mpeg2dec = library('gstmpeg2dec',
    mpeg2dec_sources,
    c_args : ugly_args,
    include_directories : [configinc],
    dependencies : [gstvideo_dep, mpeg2_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [mpeg2dec]
endif
