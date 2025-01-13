tcp_sources = [
  'gstmultihandlesink.c',
  'gstmultisocketsink.c',
  'gstsocketsrc.c',
  'gsttcpclientsrc.c',
  'gsttcpclientsink.c',
  'gsttcpelements.c',
  'gsttcpserversrc.c',
  'gsttcpserversink.c',
  'gsttcpsrcstats.c',
  'gsttcpplugin.c',
]

tcp_headers = [
  'gstsocketsrc.h',
  'gstmultisocketsink.h',
  'gsttcpclientsink.h',
  'gstmultihandlesink.h',
  'gsttcpelements.h',
  'gsttcpserversink.h',
  'gsttcpclientsrc.h',
  'gstmultifdsink.h',
  'gsttcpsrcstats.h',
  'gsttcpserversrc.h',
]

if core_conf.has('HAVE_SYS_SOCKET_H')
   tcp_sources += 'gstmultifdsink.c'
endif

doc_sources = []
foreach s: tcp_sources + tcp_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'tcp': pathsep.join(doc_sources)
}

if get_option('tcp').disabled()
  subdir_done()
endif

gsttcp = library('gsttcp',
  tcp_sources,
  c_args : gst_plugins_base_args,
  include_directories: [configinc, libsinc],
  dependencies : [gst_base_dep, gst_net_dep, gio_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gsttcp]
