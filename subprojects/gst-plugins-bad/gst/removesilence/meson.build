silence_sources = [
  'gstremovesilence.c',
  'vad_private.c',
]

silence_headers = [
  'gstremovesilence.h',
  'vad_private.h',
]

doc_sources = []
foreach s: silence_sources + silence_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'removesilence': pathsep.join(doc_sources)
}

if get_option('removesilence').disabled()
  subdir_done()
endif

gstremovesilence = library('gstremovesilence',
  silence_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstaudio_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstremovesilence]
