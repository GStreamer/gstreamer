multipart_sources = [
  'multipart.c',
  'multipartdemux.c',
  'multipartmux.c',
]

multipart_headers = [
  'multipartmux.h',
  'multipartdemux.h',
]

doc_sources = []
foreach s: multipart_sources + multipart_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'multipart': pathsep.join(doc_sources)
}

if get_option('multipart').disabled()
  subdir_done()
endif

gstmultipart = library('gstmultipart',
  multipart_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstmultipart]
