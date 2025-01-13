imagefreeze_sources = [
  'gstimagefreeze.c',
]

imagefreeze_headers = [
  'gstimagefreeze.h',
]

doc_sources = []
foreach s: imagefreeze_sources + imagefreeze_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'imagefreeze': pathsep.join(doc_sources)
}

if get_option('imagefreeze').disabled()
  subdir_done()
endif

gstimagefreeze = library('gstimagefreeze',
  imagefreeze_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc, libsinc],
  dependencies : [gst_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstimagefreeze]
