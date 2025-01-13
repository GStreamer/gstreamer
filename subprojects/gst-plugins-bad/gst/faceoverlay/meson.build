fover_sources = [
  'gstfaceoverlay.c',
]

fover_headers = [
  'gstfaceoverlay.h',
]

doc_sources = []
foreach s: fover_sources + fover_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'faceoverlay': pathsep.join(doc_sources)
}

if get_option('faceoverlay').disabled()
  subdir_done()
endif

gstfaceoverlay = library('gstfaceoverlay',
  fover_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstfaceoverlay]
