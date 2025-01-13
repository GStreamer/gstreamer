directfb_dep = dependency('directfb', version : '>= 0.9.24', required : get_option('directfb'))

directfb_sources = [
  'dfbvideosink.c',
]

directfb_headers = [
  'dfbvideosink.h',
]

doc_sources = []
foreach s: directfb_sources + directfb_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'directfb': pathsep.join(doc_sources)
}

if directfb_dep.found()
  gstdirectfb = library('gstdirectfb',
    directfb_sources,
    c_args : gst_plugins_bad_args + cc.get_supported_arguments('-Wno-redundant-decls'),
    include_directories : [configinc],
    dependencies : [gstvideo_dep, directfb_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstdirectfb]
endif
