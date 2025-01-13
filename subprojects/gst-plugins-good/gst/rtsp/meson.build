rtsp_sources = [
  'gstrtspelement.c',
  'gstrtsp.c',
  'gstrtspsrc.c',
  'gstrtpdec.c',
  'gstrtspext.c',
]

rtsp_headers = [
  'gstrtpdec.h',
  'gstrtspelements.h',
  'gstrtspsrc.h',
  'gstrtspext.h',
]

doc_sources = []
foreach s: rtsp_sources + rtsp_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'rtsp': pathsep.join(doc_sources)
}

if get_option('rtsp').disabled()
  subdir_done()
endif

gstrtsp = library('gstrtsp',
  rtsp_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstbase_dep, gstrtp_dep, gstrtsp_dep, gstsdp_dep, gstnet_dep, gio_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstrtsp]
