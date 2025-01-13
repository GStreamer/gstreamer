openmpt_dep = dependency('libopenmpt', required: get_option('openmpt'))

openmpt_sources = [
  'gstopenmptdec.c',
  'plugin.c',
]

openmpt_headers = [
  'gstopenmptdec.h',
]

doc_sources = []
foreach s: openmpt_sources + openmpt_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'openmpt': pathsep.join(doc_sources)
}

if openmpt_dep.found()
  gstopenmpt = library('gstopenmpt',
    openmpt_sources,
    c_args: gst_plugins_bad_args,
    include_directories: [configinc],
    dependencies: [gstbadaudio_dep, gstaudio_dep, openmpt_dep],
    install: true,
    install_dir: plugins_install_dir,
  )
  plugins += [gstopenmpt]
endif
