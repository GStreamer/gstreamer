voaac_sources = ['gstvoaac.c', 'gstvoaacenc.c']

voaac_headers = [
  'gstvoaacenc.h',
]

doc_sources = []
foreach s: voaac_sources + voaac_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'voaacenc': pathsep.join(doc_sources)
}

voaac_dep = dependency('vo-aacenc', required : get_option('voaacenc'))

if voaac_dep.found()
  gstvoaacenc = library('gstvoaacenc',
    voaac_sources,
    c_args : gst_plugins_bad_args,
    link_args : noseh_link_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep, gstpbutils_dep, gstaudio_dep, voaac_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstvoaacenc]
endif
