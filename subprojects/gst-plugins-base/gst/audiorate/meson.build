audiorate_sources = [
  'gstaudiorate.c',
]

audiorate_headers = [
  'gstaudiorate.h',
]

doc_sources = []
foreach s: audiorate_sources + audiorate_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'audiorate': pathsep.join(doc_sources)
}

if get_option('audiorate').disabled()
  subdir_done()
endif

audiorate = library('gstaudiorate',
  audiorate_sources,
  c_args : gst_plugins_base_args,
  include_directories: [configinc, libsinc],
  dependencies : [audio_dep, gst_base_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [audiorate]
