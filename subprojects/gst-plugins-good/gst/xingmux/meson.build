xing_sources = [
  'plugin.c',
  'gstxingmux.c',
]

xing_headers = [
  'gstxingmux.h',
]

doc_sources = []
foreach s: xing_sources + xing_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'xingmux': pathsep.join(doc_sources)
}

if get_option('xingmux').disabled()
  subdir_done()
endif

gstxingmux = library('gstxingmux',
  xing_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstxingmux]
