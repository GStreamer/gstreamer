speed_sources = [
  'gstspeed.c'
]

speed_headers = [
  'gstspeed.h',
]

doc_sources = []
foreach s: speed_sources + speed_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'speed': pathsep.join(doc_sources)
}

if get_option('speed').disabled()
  subdir_done()
endif

gstspeed = library('gstspeed',
  speed_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstaudio_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstspeed]
