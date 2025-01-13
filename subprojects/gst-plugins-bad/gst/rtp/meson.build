gst_plugins_rtp_sources = [
  'plugin.c',
  'gstrtpsink.c',
  'gstrtpsrc.c',
  'gstrtp-utils.c',
]

gst_plugins_rtp_headers = [
  'gstrtpsrc.h',
  'gstrtpsink.h',
  'gstrtp-utils.h',
]

doc_sources = []
foreach s: gst_plugins_rtp_sources + gst_plugins_rtp_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'rtpmanagerbad': pathsep.join(doc_sources)
}

if get_option('rtp').disabled()
  subdir_done()
endif

gstrtp = library('gstrtpmanagerbad',
  gst_plugins_rtp_sources,
  dependencies: [gst_dep, gstbase_dep, gstrtp_dep, gstnet_dep, gstcontroller_dep, gio_dep],
  include_directories: [configinc],
  install: true,
  c_args: gst_plugins_bad_args,
  install_dir: plugins_install_dir,
)
plugins += [gstrtp]
