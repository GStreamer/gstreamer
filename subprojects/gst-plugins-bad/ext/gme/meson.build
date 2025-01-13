gme_sources = [
  'gstgme.c',
]

gme_headers = [
  'gstgme.h',
]

doc_sources = []
foreach s: gme_sources + gme_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'gme': pathsep.join(doc_sources)
}

if get_option('gme').disabled()
  subdir_done()
endif

have_gme = cc.has_header ('gme/gme.h')
if have_gme
  gme_dep = cc.find_library('gme', required: get_option('gme'))
  have_gme = gme_dep.found()
endif

if have_gme
  cdata.set('HAVE_LIBGME_ACCURACY', cc.has_function('gme_enable_accuracy', dependencies: gme_dep))

  gstgme = library('gstgme',
    gme_sources,
    c_args: gst_plugins_bad_args,
    include_directories: [configinc],
    dependencies: [gstaudio_dep, gme_dep],
    install: true,
    install_dir: plugins_install_dir,
  )
  plugins += [gstgme]
elif get_option('gme').enabled()
  error('libgme plugin was enabled but required dependencies not found.')
endif
