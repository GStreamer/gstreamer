codecalpha_sources = [
  'gstplugin.c',
  'gstalphacombine.c',
  'gstalphadecodebin.c',
  'gstvp8alphadecodebin.c',
  'gstvp9alphadecodebin.c',
  'gstcodecalphademux.c',
]

codecalpha_headers = [
  'gstalphadecodebin.h',
  'gstalphacombine.h',
  'gstvp9alphadecodebin.h',
  'gstcodecalphademux.h',
  'gstvp8alphadecodebin.h',
]

doc_sources = []
foreach s: codecalpha_sources + codecalpha_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'codecalpha': pathsep.join(doc_sources)
}

if get_option('codecalpha').disabled()
  subdir_done()
endif

gstcodecalpha = library('gstcodecalpha',
  codecalpha_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstvideo_dep, gstpbutils_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstcodecalpha]
