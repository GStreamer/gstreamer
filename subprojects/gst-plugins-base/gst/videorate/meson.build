videorate_sources = [
  'gstvideorate.c',
]

videorate_headers = [
  'gstvideorate.h',
]

doc_sources = []
foreach s: videorate_sources + videorate_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'videorate': pathsep.join(doc_sources)
}

if get_option('videorate').disabled()
  subdir_done()
endif

gstvideorate = library('gstvideorate',
  videorate_sources,
  c_args : gst_plugins_base_args,
  include_directories: [configinc, libsinc],
  dependencies : [video_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstvideorate]
