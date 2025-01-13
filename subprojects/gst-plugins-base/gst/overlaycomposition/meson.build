overlaycomposition_sources = [
  'gstoverlaycomposition.c',
]

overlaycomposition_headers = [
  'gstoverlaycomposition.h',
]

doc_sources = []
foreach s: overlaycomposition_sources + overlaycomposition_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'overlaycomposition': pathsep.join(doc_sources)
}

if get_option('overlaycomposition').disabled()
  subdir_done()
endif

gstoverlaycomposition = library('gstoverlaycomposition',
  'gstoverlaycomposition.c',
  c_args : gst_plugins_base_args,
  include_directories: [configinc, libsinc],
  dependencies : [video_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstoverlaycomposition]
