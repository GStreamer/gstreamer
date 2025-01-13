dvdsub_sources = [
  'gstdvdsubdec.c',
  'gstdvdsubparse.c',
]

dvdsub_headers = [
  'gstdvdsubdec.h',
  'gstdvdsubparse.h',
]

doc_sources = []
foreach s: dvdsub_sources + dvdsub_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'dvdsub': pathsep.join(doc_sources)
}

if get_option('dvdsub').disabled()
  subdir_done()
endif

gstdvdsub = library('gstdvdsub',
  dvdsub_sources,
  c_args : ugly_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstdvdsub]
