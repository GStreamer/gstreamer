x265_opt = get_option('x265').require(gpl_allowed, error_message: '''
  Plugin x265 explicitly required via options but GPL-licensed plugins disabled via options.
  Pass option -Dgpl=enabled to Meson to allow GPL-licensed plugins to be built.
  ''')

x265_dep = dependency('x265', required: x265_opt)
x265_sources = [
  'gstx265enc.c',
]

x265_headers = [
  'gstx265enc.h',
]

doc_sources = []
foreach s: x265_sources + x265_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'x265': pathsep.join(doc_sources)
}

if x265_dep.found()
  gstx265 = library('gstx265',
    x265_sources,
    c_args : gst_plugins_bad_args + cc.get_supported_arguments(['-Wno-undef']),
    include_directories : [configinc],
    dependencies : [gstpbutils_dep, gstvideo_dep, x265_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstx265]
endif
