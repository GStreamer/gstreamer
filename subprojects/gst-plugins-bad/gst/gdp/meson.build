gdp_sources = [
  'dataprotocol.c',
  'gstgdpelement.c',
  'gstgdp.c',
  'gstgdppay.c',
  'gstgdpdepay.c',
]

gdp_headers = [
  'gstgdpdepay.h',
  'dp-private.h',
  'dataprotocol.h',
  'gstgdpelements.h',
  'gstgdppay.h',
]

doc_sources = []
foreach s: gdp_sources + gdp_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'gdp': pathsep.join(doc_sources)
}

if get_option('gdp').disabled()
  subdir_done()
endif

gstdgp = library('gstgdp',
  gdp_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstdgp]
