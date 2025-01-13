no_warn_args = []
# XKeycodeToKeysym is deprecated, but we use it when Xkb is unavailable
if cc.has_argument ('-Wno-deprecated-declarations')
  no_warn_args += '-Wno-deprecated-declarations'
endif

ximage_sources = [
  'ximagesink.c',
  'ximage.c',
  'ximagepool.c',
]

ximage_headers = [
  'ximagepool.h',
  'ximagesink.h',
]

doc_sources = []
foreach s: ximage_sources + ximage_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'ximagesink': pathsep.join(doc_sources)
}

if x11_dep.found()
  gstximage = library('gstximagesink',
    ximage_sources,
    c_args : gst_plugins_base_args + no_warn_args,
    include_directories: [configinc, libsinc],
    dependencies : [video_dep, gst_base_dep, gst_dep, x11_dep, xshm_dep, xi_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstximage]
endif
