monoscope_sources = [
  'gstmonoscope.c',
  'monoscope.c',
  'convolve.c',
]

monoscope_headers = [
  'gstmonoscope.h',
  'convolve.h',
  'monoscope.h',
]

doc_sources = []
foreach s: monoscope_sources + monoscope_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'monoscope': pathsep.join(doc_sources)
}

if get_option('monoscope').disabled()
  subdir_done()
endif

gstmonoscope = library('gstmonoscope',
  monoscope_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstaudio_dep, gstvideo_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstmonoscope]
