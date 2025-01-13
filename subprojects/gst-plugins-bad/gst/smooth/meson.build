smooth_sources = [
  'gstsmooth.c',
]

smooth_headers = [
  'gstsmooth.h',
]

doc_sources = []
foreach s: smooth_sources + smooth_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'smooth': pathsep.join(doc_sources)
}

if get_option('smooth').disabled()
  subdir_done()
endif

gstsmooth = library('gstsmooth',
  smooth_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstsmooth]
