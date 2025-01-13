modplug_sources = [
  'gstmodplug.cc',
]

modplug_headers = [
  'gstmodplug.h',
]

doc_sources = []
foreach s: modplug_sources + modplug_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'modplug': pathsep.join(doc_sources)
}

if not add_languages('cpp', native: false, required: get_option('modplug'))
  subdir_done()
endif

modplug_dep = dependency('libmodplug', required: get_option('modplug'))

if modplug_dep.found()
  gstmodplug = library('gstmodplug',
    modplug_sources,
    cpp_args: gst_plugins_bad_args,
    include_directories: [configinc],
    dependencies: [gstaudio_dep, modplug_dep, libm],
    install: true,
    install_dir: plugins_install_dir,
  )
  plugins += [gstmodplug]
endif
