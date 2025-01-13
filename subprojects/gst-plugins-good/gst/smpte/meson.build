smpte_sources = [
  'gstsmpte.c',
  'gstmask.c',
  'barboxwipes.c',
  'paint.c',
  'gstsmptealpha.c',
  'plugin.c',
]

smpte_headers = [
  'gstsmptealpha.h',
  'paint.h',
  'gstmask.h',
  'gstsmpte.h',
]

doc_sources = []
foreach s: smpte_sources + smpte_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'smpte': pathsep.join(doc_sources)
}

if get_option('smpte').disabled()
  subdir_done()
endif

gstsmpte = library('gstsmpte',
  smpte_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gstvideo_dep, gst_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstsmpte]
