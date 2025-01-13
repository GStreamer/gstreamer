flxdec_sources = [
  'gstflxdec.c',
  'flx_color.c',
]

flxdec_headers = [
  'gstflxdec.h',
  'flx_color.h',
  'flx_fmt.h',
]

doc_sources = []
foreach s: flxdec_sources + flxdec_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'flxdec': pathsep.join(doc_sources)
}

if get_option('flx').disabled()
  subdir_done()
endif

flxdec = library('gstflxdec',
  flxdec_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep, gst_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [flxdec]
