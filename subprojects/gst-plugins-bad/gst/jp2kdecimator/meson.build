jp2_sources = [
  'gstjp2kdecimator.c',
  'jp2kcodestream.c',
]

jp2_headers = [
  'gstjp2kdecimator.h',
  'jp2kcodestream.h',
]

doc_sources = []
foreach s: jp2_sources + jp2_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'jp2kdecimator': pathsep.join(doc_sources)
}

if get_option('jp2kdecimator').disabled()
  subdir_done()
endif

gstjp2kdecimator = library('gstjp2kdecimator',
  jp2_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstjp2kdecimator]
