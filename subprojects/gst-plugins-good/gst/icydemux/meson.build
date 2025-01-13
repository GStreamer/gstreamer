icydemux_sources = [
  'gsticydemux.c',
]

icydemux_headers = [
  'gsticydemux.h',
]

doc_sources = []
foreach s: icydemux_sources + icydemux_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'icydemux': pathsep.join(doc_sources)
}

if get_option('icydemux').disabled()
  subdir_done()
endif

gsticydemux = library('gsticydemux',
  icydemux_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc, libsinc],
  dependencies : [gst_dep, gstbase_dep, gsttag_dep, zlib_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gsticydemux]
