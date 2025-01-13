rist_sources = [
  'gstroundrobin.c',
  'gstristrtxsend.c',
  'gstristrtxreceive.c',
  'gstristsrc.c',
  'gstristsink.c',
  'gstrist.c',
  'gstristplugin.c',
  'gstristrtpext.c',
  'gstristrtpdeext.c'
]

rist_headers = [
  'gstrist.h',
  'gstroundrobin.h',
]

doc_sources = []
foreach s: rist_sources + rist_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'rist': pathsep.join(doc_sources)
}

if get_option('rist').disabled()
  subdir_done()
endif

gstrist = library('gstrist',
  rist_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstrtp_dep, gstnet_dep, gio_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstrist]
