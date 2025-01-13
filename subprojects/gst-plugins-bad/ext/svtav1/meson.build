svtav1_sources = [
  'gstsvtav1enc.c',
]

svtav1_headers = [
  'gstsvtav1enc.h',
]

doc_sources = []
foreach s: svtav1_sources + svtav1_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'svtav1': pathsep.join(doc_sources)
}

svtav1_dep = dependency('SvtAv1Enc', version : '>= 1.1', required: get_option('svtav1'))

if svtav1_dep.found()
  gstsvtav1 = library('gstsvtav1',
    svtav1_sources,
    c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
    include_directories : [configinc],
    dependencies : [gstbase_dep, gstvideo_dep, svtav1_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstsvtav1]
endif
