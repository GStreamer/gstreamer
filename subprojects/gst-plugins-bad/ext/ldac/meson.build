ldac_sources = [
  'ldac-plugin.c',
  'gstldacenc.c',
]

ldac_headers = [
  'gstldacenc.h',
]

doc_sources = []
foreach s: ldac_sources + ldac_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'ldac': pathsep.join(doc_sources)
}

ldac_dep = dependency('ldacBT-enc', required : get_option('ldac'))

if ldac_dep.found()
  gstldac = library('gstldac',
    ldac_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstaudio_dep, ldac_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstldac]
endif
