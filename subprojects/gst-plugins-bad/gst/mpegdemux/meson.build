mpegd_sources = [
  'plugin.c',
  'gstmpegdemux.c',
  'gstpesfilter.c',
]

mpegd_headers = [
  'gstpesfilter.h',
  'gstmpegdemux.h',
  'gstmpegdefs.h',
]

doc_sources = []
foreach s: mpegd_sources + mpegd_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'mpegpsdemux': pathsep.join(doc_sources)
}

if get_option('mpegdemux').disabled()
  subdir_done()
endif

gstmpedpsdemux = library('gstmpegpsdemux',
  mpegd_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gsttag_dep, gstpbutils_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstmpedpsdemux]
