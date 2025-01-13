coloreffects_sources = [
  'gstplugin.c',
  'gstcoloreffects.c',
  'gstchromahold.c',
]

coloreffects_headers = [
  'gstchromahold.h',
  'gstcoloreffects.h',
]

doc_sources = []
foreach s: coloreffects_sources + coloreffects_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'coloreffects': pathsep.join(doc_sources)
}

if get_option('coloreffects').disabled()
  subdir_done()
endif

gstcoloreffects = library('gstcoloreffects',
  coloreffects_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstcoloreffects]
