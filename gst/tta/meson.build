tta_sources = [
  'gsttta.c',
  'gstttaparse.c',
  'gstttadec.c',
]

gsttta = library('gsttta',
  tta_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep],
  install : true,
  install_dir : plugins_install_dir,
)
