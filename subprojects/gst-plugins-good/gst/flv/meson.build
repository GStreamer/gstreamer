flv_sources = [
  'gstflvdemux.c',
  'gstflvmux.c',
  'gstflvplugin.c',
  'gstflvelement.c',
]

flv_headers = [
  'gstflvelements.h',
  'gstindex.h',
  'gstflvmux.h',
  'amfdefs.h',
  'gstflvdemux.h',
]

doc_sources = []
foreach s: flv_sources + flv_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'flv': pathsep.join(doc_sources)
}

if get_option('flv').disabled()
  subdir_done()
endif

gstflv = library('gstflv',
  flv_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstpbutils_dep, gstvideo_dep, gsttag_dep, gstaudio_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstflv]
