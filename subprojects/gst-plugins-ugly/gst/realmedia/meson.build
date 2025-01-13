real_sources = [
  'rademux.c',
  'rmdemux.c',
  'rmutils.c',
  'realmedia.c'
]

real_headers = [
  'rmdemux.h',
  'rademux.h',
  'rmutils.h',
  'pnmsrc.h',
]

doc_sources = []
foreach s: real_sources + real_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'realmedia': pathsep.join(doc_sources)
}

if get_option('realmedia').disabled()
  subdir_done()
endif

gstrmdemux = library('gstrealmedia',
  real_sources,
  c_args : ugly_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstbase_dep, gstrtsp_dep, gstsdp_dep, gstpbutils_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstrmdemux]
