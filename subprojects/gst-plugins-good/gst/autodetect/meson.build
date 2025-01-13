autodetect_sources = [
  'gstautoaudiosink.c',
  'gstautoaudiosrc.c',
  'gstautodetect.c',
  'gstautodetectplugin.c',
  'gstautodetectelement.c',
  'gstautovideosink.c',
  'gstautovideosrc.c',
]

autodetect_headers = [
  'gstautodetectelements.h',
  'gstautoaudiosink.h',
  'gstautoaudiosrc.h',
  'gstautovideosrc.h',
  'gstautodetect.h',
  'gstautovideosink.h',
]

doc_sources = []
foreach s: autodetect_sources + autodetect_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'autodetect': pathsep.join(doc_sources)
}

if get_option('autodetect').disabled()
  subdir_done()
endif

gstautodetect = library('gstautodetect',
  autodetect_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gst_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstautodetect]
