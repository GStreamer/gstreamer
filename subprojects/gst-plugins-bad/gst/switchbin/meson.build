switchbin_sources = [
  'gstswitchbin.c', 'plugin.c'
]

switchbin_headers = [
  'gstswitchbin.h',
]

doc_sources = []
foreach s: switchbin_sources + switchbin_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'switchbin': pathsep.join(doc_sources)
}

if get_option('switchbin').disabled()
  subdir_done()
endif

gstswitchbin = library('gstswitchbin',
  switchbin_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gst_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstswitchbin]
