gstpbtypes_sources = [
  'gstpbtypes.c'
]

gstpbtypes_headers = [
  'gstpbtypes.h',
]

doc_sources = []
foreach s: gstpbtypes_sources + gstpbtypes_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'pbtypes': pathsep.join(doc_sources)
}

if get_option('pbtypes').disabled()
  subdir_done()
endif

gstpbtypes = library('gstpbtypes',
  gstpbtypes_sources,
  c_args : gst_plugins_base_args,
  include_directories: [configinc, libsinc],
  dependencies : [video_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstpbtypes]
