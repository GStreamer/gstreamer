festival_sources = [
  'gstfestival.c'
]

festival_headers = [
  'gstfestival.h',
]

doc_sources = []
foreach s: festival_sources + festival_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'festival': pathsep.join(doc_sources)
}

if get_option('festival').disabled()
  subdir_done()
endif

gstfestival = library('gstfestival',
  festival_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstaudio_dep] + winsock2 + network_deps,
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstfestival]
