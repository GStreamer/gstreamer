apetag_sources = [
  'gstapedemux.c',
]

apetag_headers = [
  'gstapedemux.h',
]

doc_sources = []
foreach s: apetag_sources + apetag_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'apetag': pathsep.join(doc_sources)
}

if get_option('apetag').disabled()
  subdir_done()
endif

gstapetag = library('gstapetag', apetag_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstpbutils_dep, gsttag_dep, gst_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstapetag]
