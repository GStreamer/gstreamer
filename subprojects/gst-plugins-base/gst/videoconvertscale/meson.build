videoconvertscale_sources = [
  'gstvideoconvert.c',
  'gstvideoconvertscale.c',
  'gstvideoconvertscaleplugin.c',
  'gstvideoscale.c',
]

videoconvertscale_headers = [
  'gstvideoconvert.h',
  'gstvideoscale.h',
  'gstvideoconvertscale.h',
]

doc_sources = []
foreach s: videoconvertscale_sources + videoconvertscale_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'videoconvertscale': pathsep.join(doc_sources)
}

if get_option('videoconvertscale').disabled()
  subdir_done()
endif

gstvideoconvertscale = library('gstvideoconvertscale',
  videoconvertscale_sources,
  c_args : gst_plugins_base_args,
  include_directories: [configinc, libsinc],
  dependencies : [video_dep, gst_dep, gst_base_dep],
  install : true,
  install_dir : plugins_install_dir,
)

plugins += [gstvideoconvertscale]
