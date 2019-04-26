tsmux_sources = [
  'mpegtsmuxplugin.c',
  'basetsmux.c',
  'mpegtsmux.c',
  'atscmux.c',
  'basetsmux_aac.c',
  'basetsmux_opus.c',
  'basetsmux_ttxt.c',
  'basetsmux_jpeg2000.c',
  'tsmux/tsmux.c',
  'tsmux/tsmuxstream.c'
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
pkgconfig.generate(gstmpegtsmux, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstmpegtsmux]
