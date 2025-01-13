openni2_sources = [
  'gstopenni2.cpp',
  'gstopenni2src.cpp',
]

openni2_headers = [
  'gstopenni2src.h',
]

doc_sources = []
foreach s: openni2_sources + openni2_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'openni2': pathsep.join(doc_sources)
}

if not add_languages('cpp', native: false, required: get_option('openni2'))
  subdir_done()
endif

openni2_dep = dependency('libopenni2', version: '>= 0.26', required: get_option('openni2'))

if openni2_dep.found()
  gstopenni2 = library('gstopenni2',
    openni2_sources,
    cpp_args: gst_plugins_bad_args + cc.get_supported_arguments(['-Wno-undef']),
    link_args: noseh_link_args,
    include_directories: [configinc],
    dependencies: [gstvideo_dep, openni2_dep],
    install: true,
    install_dir: plugins_install_dir,
  )
  plugins += [gstopenni2]
endif
