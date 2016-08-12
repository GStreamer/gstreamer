cdxaparse_sources = [
  'gstcdxaparse.c',
  'gstvcdparse.c',
]

gstcdxaparse = library('gstcdxaparse',
  cdxaparse_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstriff_dep],
  install : true,
  install_dir : plugins_install_dir,
)
