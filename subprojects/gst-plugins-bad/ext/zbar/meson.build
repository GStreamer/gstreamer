zbar_dep = dependency('zbar', version : '>= 0.9', required : get_option('zbar'))

zbar_sources = [
  'gstzbar.c',
]

zbar_headers = [
  'gstzbar.h',
]

doc_sources = []
foreach s: zbar_sources + zbar_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'zbar': pathsep.join(doc_sources)
}

if zbar_dep.found()
  gstzbar = library('gstzbar',
    'gstzbar.c',
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [ gstvideo_dep, zbar_dep ],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstzbar]
endif
