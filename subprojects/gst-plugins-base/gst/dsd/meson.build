dsd_sources = [
  'gstdsdconvert.c',
  'plugin.c'
  ]

dsd_headers = [
  'gstdsdconvert.h',
]

doc_sources = []
foreach s: dsd_sources + dsd_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'dsd': pathsep.join(doc_sources)
}

if get_option('dsd').disabled()
  subdir_done()
endif

gstdsd = library('gstdsd',
  dsd_sources,
  c_args : gst_plugins_base_args,
  include_directories: [configinc, libsinc],
  dependencies : [audio_dep, gst_base_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstdsd]
