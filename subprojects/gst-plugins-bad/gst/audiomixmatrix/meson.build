audiomixmatrix_sources = [
  'gstaudiomixmatrix.c',
]

audiomixmatrix_headers = [
  'gstaudiomixmatrix.h',
]

doc_sources = []
foreach s: audiomixmatrix_sources + audiomixmatrix_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'audiomixmatrix': pathsep.join(doc_sources)
}

if get_option('audiomixmatrix').disabled()
  subdir_done()
endif

gstaudiomixmatrix = library('gstaudiomixmatrix',
  audiomixmatrix_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstaudio_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstaudiomixmatrix]
