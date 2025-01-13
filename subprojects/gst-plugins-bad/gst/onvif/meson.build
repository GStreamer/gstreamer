onvif_sources = [
  'gstrtponvif.c',
  'gstrtponvifparse.c',
  'gstrtponviftimestamp.c',
]

onvif_headers = [
  'gstrtponviftimestamp.h',
  'gstrtponvifparse.h',
]

doc_sources = []
foreach s: onvif_sources + onvif_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'rtponvif': pathsep.join(doc_sources)
}

if get_option('onvif').disabled()
  subdir_done()
endif

gstrtponvif = library('gstrtponvif',
  onvif_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstrtp_dep, gstbase_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstrtponvif]
