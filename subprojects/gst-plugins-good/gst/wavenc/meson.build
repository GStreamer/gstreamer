wavenc_sources = [
  'gstwavenc.c',
]

wavenc_headers = [
  'gstwavenc.h',
]

doc_sources = []
foreach s: wavenc_sources + wavenc_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'wavenc': pathsep.join(doc_sources)
}

if get_option('wavenc').disabled()
  subdir_done()
endif

gstwavenc = library('gstwavenc',
  wavenc_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstaudio_dep, gstriff_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstwavenc]
