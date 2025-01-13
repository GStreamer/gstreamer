qrencode_dep = dependency('libqrencode', required: get_option('qroverlay'))

qroverlay_sources = [
  'gstqroverlay.c',
  'gstdebugqroverlay.c',
  'gstbaseqroverlay.c',
  'gstqroverlayelement.c',
  'gstqroverlayplugin.c',
]

qroverlay_headers = [
  'gstbaseqroverlay.h',
  'gstdebugqroverlay.h',
  'gstqroverlayelements.h',
  'gstqroverlay.h',
]

doc_sources = []
foreach s: qroverlay_sources + qroverlay_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'qroverlay': pathsep.join(doc_sources)
}

if qrencode_dep.found()
  json_dep = dependency('json-glib-1.0', fallback : ['json-glib', 'json_glib_dep'], required: get_option('qroverlay'))
  if json_dep.found()
    gstqroverlay = library('gstqroverlay',
      qroverlay_sources,
      c_args : gst_plugins_bad_args,
      include_directories : [configinc],
      dependencies : [gstvideo_dep, qrencode_dep, json_dep],
      install : true,
      install_dir : plugins_install_dir,
    )
    plugins += [gstqroverlay]
  endif
endif
