raw_sources = [
  'gstaudioparse.c',
  'gstvideoparse.c',
  'plugin.c',
]

raw_headers = [
  'gstaudioparse.h',
  'gstvideoparse.h',
]

doc_sources = []
foreach s: raw_sources + raw_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'legacyrawparse': pathsep.join(doc_sources)
}

if get_option('rawparse').disabled()
  subdir_done()
endif

gstlegacyrawparse = library('gstlegacyrawparse',
  raw_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep, gstaudio_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstlegacyrawparse]
