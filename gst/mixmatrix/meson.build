mixmat_sources = [
  'mixmatrix.c',
]

gstmixmatrix = library('gstmixmatrix',
  mixmat_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep],
  install : true,
  install_dir : plugins_install_dir,
)
