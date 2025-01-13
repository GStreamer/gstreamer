dv_sources = [
  'gstdv.c',
  'gstdvelement.c',
  'gstdvdec.c',
  'gstdvdemux.c',
  'gstsmptetimecode.c',
]

dv_headers = [
  'gstsmptetimecode.h',
  'gstdvdec.h',
  'gstdvelements.h',
  'gstdvdemux.h',
]

doc_sources = []
foreach s: dv_sources + dv_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'dv': pathsep.join(doc_sources)
}

dv_dep = dependency('libdv', version : '>= 0.100', required : get_option('dv'),
  fallback: ['dv', 'dv_dep'])

if dv_dep.found()
  gstdv = library('gstdv',
    dv_sources,
    c_args : gst_plugins_good_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstbase_dep, gsttag_dep, gstaudio_dep, gstvideo_dep, dv_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstdv]
  # FIXME
  #executable('smpte_test',
  #  'smpte_test.c', 'gstsmptetimecode.c',
  #  dependencies : [gstbase_dep, gsttag_dep, gstaudio_dep, gstvideo_dep, dv_dep],
  #  install : false)
endif
