psmux_sources = [
  'mpegpsmux.c',
  'psmux.c',
  'psmuxstream.c',
  'mpegpsmux_aac.c',
  'mpegpsmux_h264.c',
]

psmux_headers = [
  'crc.h',
  'psmuxstream.h',
  'psmux.h',
  'psmuxcommon.h',
  'mpegpsmux.h',
  'mpegpsmux_aac.h',
  'mpegpsmux_h264.h',
  'bits.h',
]

doc_sources = []
foreach s: psmux_sources + psmux_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'mpegpsmux': pathsep.join(doc_sources)
}

if get_option('mpegpsmux').disabled()
  subdir_done()
endif

gstmpegpsmux = library('gstmpegpsmux',
  psmux_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstmpegpsmux]
