tool_deps = glib_deps + [pbutils_dep, audio_dep, video_dep, tag_dep, gst_dep, gst_base_dep]

executable('gst-device-monitor-@0@'.format(api_version),
  'gst-device-monitor.c',
  install: true,
  c_args : gst_plugins_base_args,
  include_directories: [configinc],
  dependencies : tool_deps,
)

executable('gst-discoverer-@0@'.format(api_version),
  'gst-discoverer.c',
  install: true,
  c_args : gst_plugins_base_args,
  include_directories: [configinc],
  dependencies : tool_deps,
)

executable('gst-play-@0@'.format(api_version),
  'gst-play.c', 'gst-play-kb.c',
  install: true,
  c_args : gst_plugins_base_args,
  include_directories: [configinc],
  dependencies : tool_deps + [libm],
)
