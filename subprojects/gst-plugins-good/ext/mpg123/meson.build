mpg123_dep = dependency('libmpg123', version : '>= 1.3', required : get_option('mpg123'))

mpg123_sources = [
  'gstmpg123audiodec.c'
]

mpg123_headers = [
  'gstmpg123audiodec.h',
]

doc_sources = []
foreach s: mpg123_sources + mpg123_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'mpg123': pathsep.join(doc_sources)
}

if mpg123_dep.found()
  gstmpg123 = library('gstmpg123',
    mpg123_sources,
    c_args : gst_plugins_good_args,
    include_directories : [configinc],
    dependencies : [gstaudio_dep, mpg123_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstmpg123]
endif
