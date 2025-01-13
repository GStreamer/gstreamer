gstdebug_sources = [
  'gstdebug.c',
  'gstfakevideodec.c',
]

gstdebug_headers = [
  'gst/debugutils/gstdebugutilselements.h',
  'gst/debugutils/gstfakevideodec.h',
]

doc_sources = []
foreach s: gstdebug_sources + gstdebug_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'basedebug': pathsep.join(doc_sources)
}

if get_option('debugutils').disabled()
  subdir_done()
endif

gstdebug = library('gstbasedebug',
  gstdebug_sources,
  c_args: gst_plugins_base_args,
  include_directories : [configinc],
  dependencies : [gst_dep, gst_base_dep, video_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstdebug]
