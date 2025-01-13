alpha_sources = [
  'gstalpha.c',
]

alpha_headers = [
  'gstalpha.h',
]

alphacolor_sources = [
  'gstalphacolor.c',
]

alphacolor_headers = [
  'gstalphacolor.h',
]

doc_sources = []
foreach s: alphacolor_sources + alphacolor_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'alphacolor': pathsep.join(doc_sources)
}

doc_sources = []
foreach s: alpha_sources + alpha_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'alpha': pathsep.join(doc_sources)
}

if get_option('alpha').disabled()
  subdir_done()
endif

gstalpha = library('gstalpha',
  alpha_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gstvideo_dep, gst_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstalpha]

gstalphacolor = library('gstalphacolor',
  alphacolor_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gstvideo_dep, gst_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstalphacolor]
