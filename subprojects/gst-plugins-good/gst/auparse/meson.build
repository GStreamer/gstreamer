auparse_sources = [
  'gstauparse.c',
]

auparse_headers = [
  'gstauparse.h',
]

doc_sources = []
foreach s: auparse_sources + auparse_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'auparse': pathsep.join(doc_sources)
}

if get_option('auparse').disabled()
  subdir_done()
endif

gstauparse = library('gstauparse',
  auparse_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gstaudio_dep, gstbase_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstauparse]
