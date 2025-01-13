gstges_sources = ['gesplugin.c', 'gessrc.c', 'gesdemux.c', 'gesbasebin.c']

gstges_headers = [
  'gesbasebin.h',
]

gstges = library('gstges', gstges_sources,
  dependencies : [gst_dep, ges_dep],
  include_directories: [configinc],
  c_args : ges_c_args,
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstges]

doc_sources = []
foreach s: gstges_sources + gstges_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'ges': pathsep.join(doc_sources)
}