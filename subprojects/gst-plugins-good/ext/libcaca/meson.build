libcaca_dep = dependency('caca', required : get_option('libcaca'))

cacasink_sources = [
  'gstcacaplugin.c',
  'gstcacasink.c',
  'gstcacatv.c',
]

cacasink_headers = [
  'gstcacatv.h',
  'gstcacasink.h',
]

doc_sources = []
foreach s: cacasink_sources + cacasink_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'cacasink': pathsep.join(doc_sources)
}

if libcaca_dep.found()
  caca = library('gstcacasink', cacasink_sources,
    c_args : gst_plugins_good_args,
    link_args : noseh_link_args,
    include_directories : [configinc],
    dependencies : [gstvideo_dep, gstbase_dep, libcaca_dep],
    install : true,
    install_dir : plugins_install_dir
  )
  plugins += [caca]
endif
