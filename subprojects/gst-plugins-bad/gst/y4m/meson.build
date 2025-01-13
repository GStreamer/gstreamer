y4_sources = [
  'gsty4mdec.c'
]

y4_headers = [
  'gsty4mdec.h',
]

doc_sources = []
foreach s: y4_sources + y4_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'y4mdec': pathsep.join(doc_sources)
}

if get_option('y4m').disabled()
  subdir_done()
endif

gsty4mdec = library('gsty4mdec',
  y4_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gsty4mdec]
