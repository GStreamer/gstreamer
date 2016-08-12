tsmux_sources = [
  'mpegtsmux.c',
  'mpegtsmux_aac.c',
  'mpegtsmux_opus.c',
  'mpegtsmux_ttxt.c',
  'tsmux/tsmux.c',
  'tsmux/tsmuxstream.c',
]

gstmpegtsmux = library('gstmpegtsmux',
  tsmux_sources,
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories : [configinc, libsinc],
  dependencies : [gstmpegts_dep, gsttag_dep, gstpbutils_dep,
                  gstaudio_dep, gstvideo_dep, gstbase_dep],
  install : true,
  install_dir : plugins_install_dir,
)
