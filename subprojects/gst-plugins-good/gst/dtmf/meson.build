dtmf_sources = [
  'gstdtmfsrc.c',
  'gstrtpdtmfsrc.c',
  'gstrtpdtmfdepay.c',
  'gstdtmf.c'
]

dtmf_headers = [
  'gstdtmfsrc.h',
  'gstrtpdtmfdepay.h',
  'gstrtpdtmfsrc.h',
  'gstdtmfcommon.h',
]

doc_sources = []
foreach s: dtmf_sources + dtmf_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'dtmf': pathsep.join(doc_sources)
}

if get_option('dtmf').disabled()
  subdir_done()
endif

gstdtmf = library('gstdtmf',
  dtmf_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstrtp_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstdtmf]
