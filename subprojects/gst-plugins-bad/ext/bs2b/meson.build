bs2b_sources = [ 'gstbs2b.c', ]

bs2b_headers = [
  'gstbs2b.h',
]

doc_sources = []
foreach s: bs2b_sources + bs2b_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'bs2b': pathsep.join(doc_sources)
}

bs2b_dep = dependency('libbs2b', version : '>=3.1.0', required : get_option('bs2b'))

if bs2b_dep.found()
  gstbs2b = library('gstbs2b',
    bs2b_sources,
    c_args : gst_plugins_bad_args,
    link_args : noseh_link_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstaudio_dep, gstbase_dep, bs2b_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstbs2b]
endif
