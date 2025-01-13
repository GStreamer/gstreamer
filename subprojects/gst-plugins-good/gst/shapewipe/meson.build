shapewipe_sources = [
  'gstshapewipe.c',
]

shapewipe_headers = [
  'gstshapewipe.h',
]

doc_sources = []
foreach s: shapewipe_sources + shapewipe_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'shapewipe': pathsep.join(doc_sources)
}

if get_option('shapewipe').disabled()
  subdir_done()
endif

gstshapewipe = library('gstshapewipe',
  shapewipe_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gst_dep, gstvideo_dep, gio_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstshapewipe]
