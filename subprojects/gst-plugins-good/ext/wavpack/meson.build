wavpack_dep = dependency('wavpack', version : '>= 4.60.0', required : get_option('wavpack'))
wavpack_sources = [
  'gstwavpackelement.c',
  'gstwavpack.c',
  'gstwavpackcommon.c',
  'gstwavpackdec.c',
  'gstwavpackenc.c',
  'gstwavpackstreamreader.c',
]

wavpack_headers = [
  'gstwavpackcommon.h',
  'gstwavpackenc.h',
  'gstwavpackelements.h',
  'gstwavpackdec.h',
  'gstwavpackstreamreader.h',
]

doc_sources = []
foreach s: wavpack_sources + wavpack_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'wavpack': pathsep.join(doc_sources)
}


if wavpack_dep.found()
  gstwavpack = library('gstwavpack',
    wavpack_sources,
    c_args : gst_plugins_good_args,
    link_args : noseh_link_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstbase_dep, gstaudio_dep, wavpack_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstwavpack]
endif
