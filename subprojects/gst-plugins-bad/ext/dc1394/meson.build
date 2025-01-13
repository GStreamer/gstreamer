dc1394_dep = dependency('libdc1394-2', version: '>= 2.2.5', required: get_option('dc1394'))

dc1394_sources = [
  'gstdc1394src.c',
]

dc1394_headers = [
  'gstdc1394src.h',
]

doc_sources = []
foreach s: dc1394_sources + dc1394_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'dc1394': pathsep.join(doc_sources)
}

if dc1394_dep.found()
  gstdc1394 = library('gstdc1394',
    dc1394_sources,
    c_args: gst_plugins_bad_args,
    include_directories: [configinc],
    dependencies: [gstvideo_dep, dc1394_dep],
    install: true,
    install_dir: plugins_install_dir,
  )
  plugins += [gstdc1394]
endif
