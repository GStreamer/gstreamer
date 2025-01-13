vsignal_sources = [
  'gstvideosignal.c',
  'gstvideoanalyse.c',
  'gstsimplevideomarkdetect.c',
  'gstsimplevideomark.c',
]

vsignal_headers = [
  'gstvideosignal.h',
  'gstsimplevideomarkdetect.h',
  'gstvideoanalyse.h',
  'gstsimplevideomark.h',
]

doc_sources = []
foreach s: vsignal_sources + vsignal_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'videosignal': pathsep.join(doc_sources)
}

if get_option('videosignal').disabled()
  subdir_done()
endif

gstvideosignal = library('gstvideosignal',
  vsignal_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstvideosignal]
