fluidsynth_dep = dependency('fluidsynth', version : '>= 2.1', required : get_option('fluidsynth'))

fluidsynthmidi_sources = [
  'gstfluiddec.c',
]

fluidsynthmidi_headers = [
  'gstfluiddec.h',
]

doc_sources = []
foreach s: fluidsynthmidi_sources + fluidsynthmidi_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'fluidsynthmidi': pathsep.join(doc_sources)
}

if fluidsynth_dep.found()
  gstfluidsynth = library('gstfluidsynthmidi',
    fluidsynthmidi_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstaudio_dep, gst_dep, fluidsynth_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstfluidsynth]
endif
