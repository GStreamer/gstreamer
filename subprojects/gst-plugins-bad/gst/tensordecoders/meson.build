tensordecoders_sources = [
  'gsttensordecoders.c',
  'gstssdobjectdetector.c'
]

tensordecoders_headers = [
  'gstssdobjectdetector.h',
]

doc_sources = []
foreach s: tensordecoders_sources + tensordecoders_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'tensordecoders': pathsep.join(doc_sources)
}

if get_option('tensordecoders').disabled()
  subdir_done()
endif

gsttensordecoders = library('gsttensordecoders',
  tensordecoders_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep, gstanalytics_dep, gio_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gsttensordecoders]
