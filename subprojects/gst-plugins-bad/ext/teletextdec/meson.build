zvbi_dep = dependency('zvbi-0.2', required: get_option('teletext'))

teletext_sources = [
  'gstteletextdec.c',
]

teletext_headers = [
  'gstteletextdec.h',
]

doc_sources = []
foreach s: teletext_sources + teletext_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'teletext': pathsep.join(doc_sources)
}

if zvbi_dep.found()
  gstteletext = library('gstteletext',
    teletext_sources,
    c_args: gst_plugins_bad_args,
    link_args: noseh_link_args,
    include_directories: [configinc],
    dependencies: [gstvideo_dep, zvbi_dep],
    install: true,
    install_dir: plugins_install_dir,
  )
  plugins += [gstteletext]
endif
