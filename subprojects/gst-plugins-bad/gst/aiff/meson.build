aiff_sources = [
  'aiff.c', 'aiffmux.c', 'aiffparse.c', 'gstaiffelement.c'
]

aiff_headers = [
  'aiffmux.h',
  'aiffelements.h',
  'aiffparse.h',
]

doc_sources = []
foreach s: aiff_sources + aiff_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'aiff': pathsep.join(doc_sources)
}

if get_option('aiff').disabled()
  subdir_done()
endif

gstaiff = library('gstaiff',
  aiff_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstbase_dep, gsttag_dep, gstaudio_dep, gstpbutils_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstaiff]
