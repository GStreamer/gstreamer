insertbin_sources = [
  'plugin.c'
]

insertbin_headers = [
]

doc_sources = []
foreach s: insertbin_sources + insertbin_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'insertbin': pathsep.join(doc_sources)
}

if get_option('insertbin').disabled()
  subdir_done()
endif

gstinsertbin = library('gstinsertbin',
  insertbin_sources,
  c_args : gst_plugins_bad_args + [ '-DGST_USE_UNSTABLE_API' ],
  include_directories : [configinc],
  dependencies : [gst_dep, gstinsertbin_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstinsertbin]
