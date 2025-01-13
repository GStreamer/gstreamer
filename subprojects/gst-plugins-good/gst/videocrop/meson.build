videocrop_sources = [
  'gstvideocrop.c',
  'gstaspectratiocrop.c',
  'gstvideocropplugin.c',
]

videocrop_headers = [
  'gstaspectratiocrop.h',
  'gstvideocrop-private.h',
  'gstvideocrop.h',
]

doc_sources = []
foreach s: videocrop_sources + videocrop_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'videocrop': pathsep.join(doc_sources)
}

if get_option('videocrop').disabled()
  subdir_done()
endif

gstvideocrop = library('gstvideocrop',
  videocrop_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc, libsinc],
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstvideocrop]
