gstadpcmenc_sources = [
  'adpcmenc.c'
]

gstadpcmenc_headers = [
]

doc_sources = []
foreach s: gstadpcmenc_sources + gstadpcmenc_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'adpcmenc': pathsep.join(doc_sources)
}

if get_option('adpcmenc').disabled()
  subdir_done()
endif

gstadpcmenc = library('gstadpcmenc',
  gstadpcmenc_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstaudio_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstadpcmenc]
