transcode_sources = [
  'gsttranscodeelement.c',
  'gsttranscodeplugin.c',
  'gsttranscodebin.c',
  'gst-cpu-throttling-clock.c',
  'gsturitranscodebin.c',
]

transcode_headers = [
  'gst-cpu-throttling-clock.h',
  'gsttranscoding.h',
  'gsttranscodeelements.h',
]

doc_sources = []
foreach s: transcode_sources + transcode_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'transcode': pathsep.join(doc_sources)
}

if get_option('transcode').disabled()
  subdir_done()
endif

gsttranscoder_plugin = library('gsttranscode',
  transcode_sources,
  install : true,
  dependencies : [gst_dep, gstpbutils_dep],
  c_args : gst_plugins_bad_args,
  include_directories : [configinc, libsinc],
  install_dir : plugins_install_dir,
)

plugins += [gsttranscoder_plugin]
