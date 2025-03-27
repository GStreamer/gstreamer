y4m_sources = [
  'plugin.c',
  'gsty4menc.c',
  'gsty4mdec.c'
]

y4m_headers = [
  'gsty4menc.h',
  'gsty4mdec.h',
]

doc_sources = []
foreach s: y4m_sources + y4m_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'y4m': pathsep.join(doc_sources)
}

if get_option('y4m').disabled()
  subdir_done()
endif

gsty4m = library('gsty4m',
  y4m_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gsty4m]
