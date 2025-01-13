a52dec_opt = get_option('a52dec').require(gpl_allowed, error_message: '''
  Plugin a52dec explicitly required via options but GPL-licensed plugins disabled via options.
  Pass option -Dgpl=enabled to Meson to allow GPL-licensed plugins to be built.
  ''')

a52dec_sources = [
  'gsta52dec.c',
]

a52dec_headers = [
  'gsta52dec.h',
]

doc_sources = []
foreach s: a52dec_sources + a52dec_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'a52dec': pathsep.join(doc_sources)
}

a52_dep = cc.find_library('a52', required : get_option('a52dec'))
if a52_dep.found()
  have_a52_h = cc.has_header_symbol('a52dec/a52.h', 'a52_init', prefix : '#include <stdint.h>')

  if have_a52_h
    a52dec = library('gsta52dec',
      a52dec_sources,
      c_args : ugly_args,
      include_directories : [configinc],
      dependencies : [gstaudio_dep, orc_dep, a52_dep],
      install : true,
      install_dir : plugins_install_dir,
    )
    plugins += [a52dec]
  elif a52dec_opt.enabled()
    error('a52dec plugin enabled but a52.h not found')
  endif
endif
