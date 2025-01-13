ivfparse_sources = [
  'gstivfparse.c',
]

ivfparse_headers = [
  'gstivfparse.h',
]

doc_sources = []
foreach s: ivfparse_sources + ivfparse_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'ivfparse': pathsep.join(doc_sources)
}

if get_option('ivfparse').disabled()
  subdir_done()
endif

gstivfparse = library('gstivfparse',
  ivfparse_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstivfparse]
