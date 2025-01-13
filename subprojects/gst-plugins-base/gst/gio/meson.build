gio_sources = ['gstgio.c',
  'gstgioelement.c',
  'gstgiobasesink.c',
  'gstgiobasesrc.c',
  'gstgiosink.c',
  'gstgiosrc.c',
  'gstgiostreamsink.c',
  'gstgiostreamsrc.c',
]

gio_headers = [
  'gstgiosink.h',
  'gstgiobasesink.h',
  'gstgiosrc.h',
  'gstgiobasesrc.h',
  'gstgiostreamsrc.h',
  'gstgiostreamsink.h',
  'gstgioelements.h',
]

doc_sources = []
foreach s: gio_sources + gio_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'gio': pathsep.join(doc_sources)
}

if get_option('gio').disabled()
  subdir_done()
endif

gstgio = library('gstgio',
  gio_sources,
  c_args : gst_plugins_base_args,
  include_directories: [configinc, libsinc],
  dependencies : [gst_base_dep, gio_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstgio]
