subenc_sources = [
  'gstsrtenc.c',
  'gstsubenc.c',
  'gstwebvttenc.c',
]

subenc_headers = [
  'gstwebvttenc.h',
  'gstsrtenc.h',
]

doc_sources = []
foreach s: subenc_sources + subenc_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'subenc': pathsep.join(doc_sources)
}

if get_option('subenc').disabled()
  subdir_done()
endif

gstsubenc = library('gstsubenc',
  subenc_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstsubenc]
