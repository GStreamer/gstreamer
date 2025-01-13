alaw_sources = [
  'alaw-encode.c',
  'alaw-decode.c',
  'alaw.c',
]

alaw_headers = [
  'alaw-decode.h',
  'alaw-encode.h',
]

mulaw_sources = [
  'mulaw-encode.c',
  'mulaw-conversion.c',
  'mulaw-decode.c',
  'mulaw.c',
]

mulaw_headers = [
  'mulaw-conversion.h',
  'mulaw-decode.h',
  'mulaw-encode.h',
]

doc_sources = []
foreach s: alaw_sources + alaw_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'alaw': pathsep.join(doc_sources)
}

doc_sources = []
foreach s: mulaw_sources + mulaw_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'mulaw': pathsep.join(doc_sources)
}

if get_option('law').disabled()
  subdir_done()
endif

gstalaw = library('gstalaw',
  alaw_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstaudio_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstalaw]

gstmulaw = library('gstmulaw',
  mulaw_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstaudio_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstmulaw]
