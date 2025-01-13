vmnc_sources = [
  'vmncdec.c',
]

vmnc_headers = [
  'vmncdec.h',
]

doc_sources = []
foreach s: vmnc_sources + vmnc_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'vmnc': pathsep.join(doc_sources)
}

if get_option('vmnc').disabled()
  subdir_done()
endif

gstvmnc = library('gstvmnc',
  vmnc_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstvmnc]
