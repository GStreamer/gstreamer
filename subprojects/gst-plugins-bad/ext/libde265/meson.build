de265_sources = [
  'gstlibde265.c',
  'libde265-dec.c',
]

de265_headers = [
  'libde265-dec.h',
]

doc_sources = []
foreach s: de265_sources + de265_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'de265': pathsep.join(doc_sources)
}

de265_dep = dependency('libde265', version : '>= 0.9', required : get_option('libde265'))

if de265_dep.found()
  gstde265 = library('gstde265',
    de265_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstvideo_dep, de265_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstde265]
endif
