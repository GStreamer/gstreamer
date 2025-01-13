proxy_sources = [
  'gstproxy.c',
  'gstproxysink.c',
  'gstproxysrc.c'
]

proxy_headers = [
  'gstproxysrc.h',
  'gstproxy-priv.h',
  'gstproxysink.h',
]

doc_sources = []
foreach s: proxy_sources + proxy_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'proxy': pathsep.join(doc_sources)
}

if get_option('proxy').disabled()
  subdir_done()
endif

gstproxy = library('gstproxy',
  proxy_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstproxy]
