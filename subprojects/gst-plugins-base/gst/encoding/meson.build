encoding_sources = [
  'gstencodebasebin.c',
  'gstencodebin.c',
  'gstencodebin2.c',
  'gstsmartencoder.c',
  'gststreamcombiner.c',
  'gststreamsplitter.c',
  'plugin.c',
  'gstencodingelements.c',
]

encoding_headers = [
  'gstsmartencoder.h',
  'gststreamsplitter.h',
  'gstencodebasebin.h',
  'gststreamcombinerpad.h',
  'gstencodebin2.h',
  'gstencodebin.h',
  'gstencodingelements.h',
  'gststreamcombiner.h',
]

doc_sources = []
foreach s: encoding_sources + encoding_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'encoding': pathsep.join(doc_sources)
}

if get_option('encoding').disabled()
  subdir_done()
endif

gstencoding = library('gstencoding',
  encoding_sources,
  c_args : gst_plugins_base_args,
  include_directories: [configinc, libsinc],
  dependencies : [pbutils_dep, video_dep, gst_base_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstencoding]
