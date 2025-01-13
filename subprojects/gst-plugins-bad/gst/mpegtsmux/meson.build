tsmux_sources = [
  'gstmpegtsmuxplugin.c',
  'gstbasetsmux.c',
  'gstbasetsmuxaac.c',
  'gstbasetsmuxopus.c',
  'gstbasetsmuxttxt.c',
  'gstbasetsmuxjpeg2000.c',
  'gstbasetsmuxjpegxs.c',
  'gstmpegtsmux.c',
  'gstatscmux.c',
  'tsmux/tsmux.c',
  'tsmux/tsmuxstream.c'
]

tsmux_headers = [
  'gstbasetsmuxopus.h',
  'gstatscmux.h',
  'gstbasetsmuxjpeg2000.h',
  'gstbasetsmuxjpegxs.h',
  'gstmpegtsmux.h',
  'gstbasetsmux.h',
  'gstbasetsmuxaac.h',
  'gstbasetsmuxttxt.h',
]

doc_sources = []
foreach s: tsmux_sources + tsmux_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'mpegtsmux': pathsep.join(doc_sources)
}

if get_option('mpegtsmux').disabled()
  subdir_done()
endif

gstmpegtsmux = library('gstmpegtsmux',
  tsmux_sources,
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories : [configinc, libsinc],
  dependencies : [gstmpegts_dep, gsttag_dep, gstpbutils_dep,
                  gstaudio_dep, gstvideo_dep, gstbase_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstmpegtsmux]
