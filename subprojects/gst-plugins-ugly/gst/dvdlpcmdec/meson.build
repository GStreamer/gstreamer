dvdpl_sources = [
  'gstdvdlpcmdec.c'
]

dvdpl_headers = [
  'gstdvdlpcmdec.h',
]

doc_sources = []
foreach s: dvdpl_sources + dvdpl_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'dvdlpcmdec': pathsep.join(doc_sources)
}

if get_option('dvdlpcmdec').disabled()
  subdir_done()
endif

gstdvdlpcmdec = library('gstdvdlpcmdec',
  dvdpl_sources,
  c_args : ugly_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstaudio_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstdvdlpcmdec]
