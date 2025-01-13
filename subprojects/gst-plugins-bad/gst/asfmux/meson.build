asfmux_sources = [
  'gstasfmux.c',
  'gstasfobjects.c',
  'gstasfparse.c',
  'gstrtpasfpay.c',
  'gstasf.c',
]

asfmux_headers = [
  'gstasfobjects.h',
  'gstasfmux.h',
  'gstrtpasfpay.h',
  'gstasfparse.h',
]

doc_sources = []
foreach s: asfmux_sources + asfmux_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'asfmux': pathsep.join(doc_sources)
}

if get_option('asfmux').disabled()
  subdir_done()
endif

gstasfmux = library('gstasfmux',
  asfmux_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstbase_dep, gstrtp_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstasfmux]
