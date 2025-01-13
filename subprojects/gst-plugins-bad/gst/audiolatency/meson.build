audiolatency_sources = [
  'gstaudiolatency.c',
]

audiolatency_headers = [
  'gstaudiolatency.h',
]

doc_sources = []
foreach s: audiolatency_sources + audiolatency_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'audiolatency': pathsep.join(doc_sources)
}

if get_option('audiolatency').disabled()
  subdir_done()
endif

gstaudiolatency = library('gstaudiolatency',
  audiolatency_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstaudiolatency]
