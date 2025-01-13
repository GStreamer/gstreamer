adpcmdec_sources = [
  'adpcmdec.c'
]

adpcmdec_headers = [
]

doc_sources = []
foreach s: adpcmdec_sources + adpcmdec_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'adpcmdec': pathsep.join(doc_sources)
}

if get_option('adpcmdec').disabled()
  subdir_done()
endif

gstadpcmdec = library('gstadpcmdec',
  adpcmdec_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstaudio_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstadpcmdec]
