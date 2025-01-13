frei0r_sources = [
  'gstfrei0r.c',
  'gstfrei0rfilter.c',
  'gstfrei0rsrc.c',
  'gstfrei0rmixer.c',
]

frei0r_headers = [
  'gstfrei0r.h',
  'gstfrei0rmixer.h',
  'frei0r.h',
  'gstfrei0rsrc.h',
  'gstfrei0rfilter.h',
]

doc_sources = []
foreach s: frei0r_sources + frei0r_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'frei0r': pathsep.join(doc_sources)
}

if get_option('frei0r').disabled()
  subdir_done()
endif

gstfrei0r = library('gstfrei0r',
  frei0r_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep, gmodule_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstfrei0r]
