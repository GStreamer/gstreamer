rtmp2_sources = [
  'gstrtmp2.c',
  'gstrtmp2element.c',
  'gstrtmp2locationhandler.c',
  'gstrtmp2sink.c',
  'gstrtmp2src.c',
  'rtmp/amf.c',
  'rtmp/rtmpchunkstream.c',
  'rtmp/rtmpclient.c',
  'rtmp/rtmpconnection.c',
  'rtmp/rtmphandshake.c',
  'rtmp/rtmpmessage.c',
  'rtmp/rtmputils.c',
]

rtmp2_headers = [
  'gstrtmp2locationhandler.h',
  'gstrtmp2elements.h',
  'gstrtmp2sink.h',
  'gstrtmp2src.h',
]

doc_sources = []
foreach s: rtmp2_sources + rtmp2_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'rtmp2': pathsep.join(doc_sources)
}

if get_option('rtmp2').disabled()
  subdir_done()
endif

gstrtmp2 = library('gstrtmp2',
  rtmp2_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstbase_dep, gio_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstrtmp2]
