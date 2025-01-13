audiofxbad_sources = [
  'gstaudiofxbad.c',
  'gstaudiochannelmix.c',
]

audiofxbad_headers = [
  'gstaudiochannelmix.h',
]

doc_sources = []
foreach s: audiofxbad_sources + audiofxbad_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'audiofxbad': pathsep.join(doc_sources)
}

if get_option('audiofxbad').disabled()
  subdir_done()
endif

gstaudiofxbad = library('gstaudiofxbad',
  audiofxbad_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstaudio_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstaudiofxbad]
