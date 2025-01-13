openal_dep = dependency('openal', method: 'pkg-config', version: '>= 1.14', required: get_option('openal'))

openal_sources = [
  'gstopenal.c',
  'gstopenalelement.c',
  'gstopenalsink.c',
  'gstopenalsrc.c',
]

openal_headers = [
  'gstopenalelements.h',
  'gstopenalsink.h',
  'gstopenalsrc.h',
]

doc_sources = []
foreach s: openal_sources + openal_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'openal': pathsep.join(doc_sources)
}

if openal_dep.found()
  gstopenal = library('gstopenal',
    openal_sources,
    c_args: gst_plugins_bad_args,
    include_directories: [configinc, libsinc],
    dependencies: [gstaudio_dep, openal_dep],
    install: true,
    install_dir: plugins_install_dir,
  )
  plugins += [gstopenal]
endif
