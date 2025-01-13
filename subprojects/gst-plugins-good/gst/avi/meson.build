avi_sources = [
  'gstavielement.c',
  'gstavi.c',
  'gstavimux.c',
  'gstavidemux.c',
  'gstavisubtitle.c'
]

avi_headers = [
  'gstavielements.h',
  'avi-ids.h',
  'gstavidemux.h',
  'gstavisubtitle.h',
  'gstavimux.h',
]

doc_sources = []
foreach s: avi_sources + avi_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'avi': pathsep.join(doc_sources)
}

if get_option('avi').disabled()
  subdir_done()
endif

gstavi = library('gstavi',
  avi_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc, libsinc],
  dependencies : [gst_dep, gstriff_dep, gstaudio_dep, gstvideo_dep, gsttag_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstavi]
