sndfile_dep = dependency('sndfile', version: '>= 1.0.16', required: get_option('sndfile'))

sndfile_sources = [
  'gstsf.c',
  'gstsfelement.c',
  'gstsfdec.c',
  # 'gstsfsink.c',
  # 'gstsfsrc.c',
]

sndfile_headers = [
  'gstsfelements.h',
  'gstsfdec.h',
  'gstsfsrc.h',
  'gstsfsink.h',
]

doc_sources = []
foreach s: sndfile_sources + sndfile_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'sndfile': pathsep.join(doc_sources)
}

if sndfile_dep.found()
  gstsndfile = library('gstsndfile',
    sndfile_sources,
    c_args: gst_plugins_bad_args,
    include_directories: [configinc, libsinc],
    dependencies: [gstaudio_dep, gst_dep, sndfile_dep],
    install: true,
    install_dir: plugins_install_dir,
  )
  plugins += [gstsndfile]
endif
