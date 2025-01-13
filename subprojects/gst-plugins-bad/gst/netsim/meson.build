netsim_sources = [
  'gstnetsim.c',
]

netsim_headers = [
  'gstnetsim.h',
]

doc_sources = []
foreach s: netsim_sources + netsim_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'netsim': pathsep.join(doc_sources)
}

if get_option('netsim').disabled()
  subdir_done()
endif

gstnetsim = library('gstnetsim',
  netsim_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstnetsim]
