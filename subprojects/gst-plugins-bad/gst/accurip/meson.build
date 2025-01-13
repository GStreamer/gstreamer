accurip_sources = [
  'gstaccurip.c',
]

accurip_headers = [
  'gstaccurip.h',
]

doc_sources = []
foreach s: accurip_sources + accurip_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'accurip': pathsep.join(doc_sources)
}

if get_option('accurip').disabled()
  subdir_done()
endif

gstaccurip = library('gstaccurip',
  accurip_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstaudio_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstaccurip]
