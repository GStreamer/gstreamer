id3demux_sources = [
  'gstid3demux.c',
]

id3demux_headers = [
  'gstid3demux.h',
]

doc_sources = []
foreach s: id3demux_sources + id3demux_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'id3demux': pathsep.join(doc_sources)
}

if get_option('id3demux').disabled()
  subdir_done()
endif

gstid3demux = library('gstid3demux',
  id3demux_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc, libsinc],
  dependencies : [gst_dep, gstbase_dep, gsttag_dep, gstpbutils_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstid3demux]
