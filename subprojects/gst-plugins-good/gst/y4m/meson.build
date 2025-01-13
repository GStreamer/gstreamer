y4menc_sources = [
  'gsty4mencode.c',
]

y4menc_headers = [
  'gsty4mencode.h',
]

doc_sources = []
foreach s: y4menc_sources + y4menc_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'y4menc': pathsep.join(doc_sources)
}

if get_option('y4m').disabled()
  subdir_done()
endif

gsty4menc = library('gsty4menc',
  y4menc_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gsty4menc]
