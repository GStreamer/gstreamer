ivtc_sources = [
  'gstivtc.c',
  'gstcombdetect.c',
]

ivtc_headers = [
  'gstivtc.h',
  'gstcombdetect.h',
]

doc_sources = []
foreach s: ivtc_sources + ivtc_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'ivtc': pathsep.join(doc_sources)
}

if get_option('ivtc').disabled()
  subdir_done()
endif

gstivtc = library('gstivtc',
  ivtc_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstivtc]
