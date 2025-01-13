audiobuffersplit_sources = [
  'gstaudiobuffersplit.c',
]

audiobuffersplit_headers = [
  'gstaudiobuffersplit.h',
]

doc_sources = []
foreach s: audiobuffersplit_sources + audiobuffersplit_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'audiobuffersplit': pathsep.join(doc_sources)
}

if get_option('audiobuffersplit').disabled()
  subdir_done()
endif

gstaudiobuffersplit = library('gstaudiobuffersplit',
  audiobuffersplit_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstaudio_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstaudiobuffersplit]
