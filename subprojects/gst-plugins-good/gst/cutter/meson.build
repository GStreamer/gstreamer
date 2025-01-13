cutter_sources = [
  'gstcutter.c',
]

cutter_headers = [
  'gstcutter.h',
]

doc_sources = []
foreach s: cutter_sources + cutter_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'cutter': pathsep.join(doc_sources)
}

if get_option('cutter').disabled()
  subdir_done()
endif

gstcutter = library('gstcutter', cutter_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstaudio_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstcutter]
