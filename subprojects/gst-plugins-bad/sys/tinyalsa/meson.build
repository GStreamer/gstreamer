tinyalsa_sources = [
  'tinyalsa.c',
  'tinyalsasink.c',
]

tinyalsa_headers = [
  'tinyalsasink.h',
]

doc_sources = []
foreach s: tinyalsa_sources + tinyalsa_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'tinyalsa': pathsep.join(doc_sources)
}


if get_option('tinyalsa').disabled()
  subdir_done()
endif

tinyalsa_dep = dependency('tinyalsa', required: get_option('tinyalsa'),
                          version: '>= 1.1.0',
                          fallback: ['tinyalsa', 'tinyalsa_dep'])
have_tinyalsa = tinyalsa_dep.found()

if have_tinyalsa
  gsttinyalsa = library('gsttinyalsa',
    tinyalsa_sources,
    c_args: gst_plugins_bad_args,
    include_directories: [configinc],
    dependencies : [gstbase_dep, gstaudio_dep, tinyalsa_dep],
    install: true,
    install_dir: plugins_install_dir
  )
  plugins += [gsttinyalsa]
elif get_option('tinyalsa').enabled()
  error('tinyalsa plugin enabled but TinyALSA library or headers not found')
endif
