subover_sources = [
  'dvb-sub.c',
  'gstdvbsuboverlay.c',
]

subover_headers = [
  'dvb-sub.h',
  'gstdvbsuboverlay.h',
]

doc_sources = []
foreach s: subover_sources + subover_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'dvbsuboverlay': pathsep.join(doc_sources)
}

if get_option('dvbsuboverlay').disabled()
  subdir_done()
endif

gstdvbsuboverlay = library('gstdvbsuboverlay',
  subover_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstbase_dep, gstvideo_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstdvbsuboverlay]
