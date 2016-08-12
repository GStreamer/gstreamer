gstplayer_sources = [
  'gstplayer.c',
  'gstplayer-signal-dispatcher.c',
  'gstplayer-video-renderer.c',
  'gstplayer-media-info.c',
  'gstplayer-g-main-context-signal-dispatcher.c',
  'gstplayer-video-overlay-video-renderer.c',
  'gstplayer-visualization.c',
]

gstplayer_headers = [
  'player.h',
  'gstplayer.h',
  'gstplayer-types.h',
  'gstplayer-signal-dispatcher.h',
  'gstplayer-video-renderer.h',
  'gstplayer-media-info.h',
  'gstplayer-g-main-context-signal-dispatcher.h',
  'gstplayer-video-overlay-video-renderer.h',
  'gstplayer-visualization.h',
]

install_headers(gstplayer_headers, subdir : 'gstreamer-' + api_version + '/gst/player/')

gstplayer = library('gstplayer-' + api_version,
  gstplayer_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gstbase_dep, gstvideo_dep, gstaudio_dep,
                  gsttag_dep, gstpbutils_dep],
  vs_module_defs: vs_module_defs_dir + 'libgstplayer.def',
)
# TODO: gir

gstplayer_dep = declare_dependency(link_with : gstplayer,
  include_directories : [libsinc],
  dependencies : [gstbase_dep, gstvideo_dep, gstaudio_dep,
                  gsttag_dep, gstpbutils_dep])
