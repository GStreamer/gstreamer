raw_sources = [
  'gstunalignedaudioparse.c',
  'gstunalignedvideoparse.c',
  'gstrawbaseparse.c',
  'gstrawaudioparse.c',
  'gstrawvideoparse.c',
  'plugin.c',
]

raw_headers = [
  'gstrawaudioparse.h',
  'gstrawbaseparse.h',
  'gstrawvideoparse.h',
  'gstunalignedvideoparse.h',
  'gstunalignedaudioparse.h',
  'gstrawparseelements.h',
  'unalignedaudio.h',
  'unalignedvideo.h',
]

doc_sources = []
foreach s: raw_sources + raw_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'rawparse': pathsep.join(doc_sources)
}

if get_option('rawparse').disabled()
  subdir_done()
endif

gstrawparse = library('gstrawparse',
  raw_sources,
  c_args : gst_plugins_base_args,
  include_directories : [configinc],
  dependencies : [gst_base_dep, video_dep, audio_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstrawparse]
