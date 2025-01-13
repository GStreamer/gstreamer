segment_sources = [
  'gstsegmentclip.c',
  'gstaudiosegmentclip.c',
  'gstvideosegmentclip.c',
  'plugin.c',
]

segment_headers = [
  'gstsegmentclip.h',
  'gstvideosegmentclip.h',
  'gstaudiosegmentclip.h',
]

doc_sources = []
foreach s: segment_sources + segment_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'segmentclip': pathsep.join(doc_sources)
}

if get_option('segmentclip').disabled()
  subdir_done()
endif

gstsegmentclip = library('gstsegmentclip',
  segment_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstaudio_dep, gstvideo_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstsegmentclip]
