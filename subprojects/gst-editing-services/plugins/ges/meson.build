gstges_sources = ['gesplugin.c', 'gessrc.c', 'gesdemux.c', 'gesbasebin.c']

gstges = library('gstges', gstges_sources,
  dependencies : [gst_dep, ges_dep],
  include_directories: [configinc],
  c_args : ges_c_args,
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstges]