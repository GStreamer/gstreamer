lc3_sources = [
  'lc3-plugin.c',
  'gstlc3dec.c',
  'gstlc3enc.c',
]

lc3_headers = [
  'gstlc3dec.h',
  'gstlc3common.h',
  'gstlc3enc.h',
]

doc_sources = []
foreach s: lc3_sources + lc3_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'lc3': pathsep.join(doc_sources)
}

lc3_dep = dependency('lc3', required:get_option ('lc3'))

if lc3_dep.found()
  gstlc3 = library('gstlc3',
    lc3_sources,
    c_args: gst_plugins_bad_args,
    include_directories: [configinc],
    dependencies: [gstaudio_dep, lc3_dep],
    install: true,
    install_dir: plugins_install_dir,
  )
  pkgconfig.generate(gstlc3, install_dir: plugins_pkgconfig_install_dir)
  plugins += [gstlc3]
endif
