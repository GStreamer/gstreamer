ass_dep = dependency('libass', version : '>= 0.10.2', required : get_option('assrender'))

assrender_sources = [
  'gstassrender.c',
]

assrender_headers = [
  'gstassrender.h',
]

doc_sources = []
foreach s: assrender_sources + assrender_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'assrender': pathsep.join(doc_sources)
}

if ass_dep.found()
  gstassrender = library('gstassrender',
    assrender_sources,
    c_args : gst_plugins_bad_args,
    link_args : noseh_link_args,
    include_directories : [configinc],
    dependencies : [gstvideo_dep, ass_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstassrender]
endif
