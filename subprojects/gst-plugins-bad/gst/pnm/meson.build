pnm_sources = [
  'gstpnmutils.c',
  'gstpnm.c',
  'gstpnmdec.c',
  'gstpnmenc.c',
]

pnm_headers = [
  'gstpnmdec.h',
  'gstpnmenc.h',
  'gstpnmutils.h',
]

doc_sources = []
foreach s: pnm_sources + pnm_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'pnm': pathsep.join(doc_sources)
}

if get_option('pnm').disabled()
  subdir_done()
endif

gstpnm = library('gstpnm',
  pnm_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstpnm]
