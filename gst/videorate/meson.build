gstvideorate = library('gstvideorate',
  'gstvideorate.c',
  c_args : gst_plugins_base_args,
  include_directories: [configinc, libsinc],
  dependencies : [video_dep],
  install : true,
  install_dir : plugins_install_dir,
)
pkgconfig.generate(gstvideorate, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstvideorate]
