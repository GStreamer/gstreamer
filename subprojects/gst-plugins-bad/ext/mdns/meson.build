microdns_dep = dependency('microdns', required: get_option('microdns'),
  fallback: ['libmicrodns', 'mdns_dep'])

microdns_sources = [
  'gstmicrodns.c',
  'gstmicrodnsdevice.c',
]

microdns_headers = [
  'gstmicrodnsdevice.h',
]

doc_sources = []
foreach s: microdns_sources + microdns_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'microdns': pathsep.join(doc_sources)
}

if microdns_dep.found()
  incdirs = [configinc]

  gstmicrodns = library('gstmicrodns',
    microdns_sources,
    c_args : gst_plugins_bad_args,
    include_directories : incdirs,
    dependencies : [gst_dep, microdns_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstmicrodns]
endif
