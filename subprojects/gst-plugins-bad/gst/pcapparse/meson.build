capp_sources = [
  'gstpcapparse.c',
  'gstirtspparse.c',
  'plugin.c',
]

capp_headers = [
  'gstpcapparse.h',
  'gstirtspparse.h',
]

doc_sources = []
foreach s: capp_sources + capp_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'pcapparse': pathsep.join(doc_sources)
}

if get_option('pcapparse').disabled()
  subdir_done()
endif

gstpcapparse = library('gstpcapparse',
  capp_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep] + winsock2,
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstpcapparse]
