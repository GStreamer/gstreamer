app_sources = [
  'gstapp.c',
  'gstappsink.c',
  'gstappsrc.c',
]

app_headers = [
  'gstappelements.h',
]

doc_sources = []
foreach s: app_sources + app_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'app': pathsep.join(doc_sources)
}

if get_option('app').disabled()
  subdir_done()
endif

gstapp_plugin = library('gstapp',
  app_sources,
  c_args: gst_plugins_base_args,
  include_directories: [configinc],
  dependencies : [gst_base_dep, app_dep, tag_dep],
  install : true,
  install_dir : plugins_install_dir,
)

plugins += [gstapp_plugin]
