chromaprint_dep = dependency('libchromaprint', required : get_option('chromaprint'))

chromaprint_sources = [
  'gstchromaprint.c',
]

chromaprint_headers = [
  'gstchromaprint.h',
]

doc_sources = []
foreach s: chromaprint_sources + chromaprint_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'chromaprint': pathsep.join(doc_sources)
}

if chromaprint_dep.found()
  gstchromaprint = library('gstchromaprint',
    chromaprint_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstaudio_dep, chromaprint_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstchromaprint]
endif
