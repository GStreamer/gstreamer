udp_sources = [
  'gstudp.c',
  'gstudpelement.c',
  'gstudpsrc.c',
  'gstudpsink.c',
  'gstmultiudpsink.c',
  'gstdynudpsink.c',
  'gstudpnetutils.c'
]

udp_headers = [
  'gstmultiudpsink.h',
  'gstudpnetutils.h',
  'gstudpelements.h',
  'gstudpsrc.h',
  'gstdynudpsink.h',
  'gstudpsink.h',
]

doc_sources = []
foreach s: udp_sources + udp_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'udp': pathsep.join(doc_sources)
}

if get_option('udp').disabled()
  subdir_done()
endif

gstudp = library('gstudp',
  udp_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc, libsinc],
  dependencies : [gst_dep, gstbase_dep, gstnet_dep, gio_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstudp]
