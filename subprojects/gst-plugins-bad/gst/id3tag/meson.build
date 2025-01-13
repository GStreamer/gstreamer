id3_sources = [
  'gstid3mux.c',
  'id3tag.c',
]

id3_headers = [
  'id3tag.h',
  'gstid3mux.h',
]

doc_sources = []
foreach s: id3_sources + id3_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'id3tag': pathsep.join(doc_sources)
}

if get_option('id3tag').disabled()
  subdir_done()
endif

gstid3tag = library('gstid3tag',
  id3_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gsttag_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstid3tag]
