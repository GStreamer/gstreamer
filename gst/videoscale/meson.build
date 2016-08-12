videoscale_sources = [
  'gstvideoscale.c',
]

gstvideoscale = library('gstvideoscale',
  videoscale_sources,
  c_args : gst_plugins_base_args,
  include_directories: [configinc, libsinc],
  dependencies : glib_deps + [video_dep, gst_dep, gst_base_dep],
  install : true,
  install_dir : plugins_install_dir,
)
