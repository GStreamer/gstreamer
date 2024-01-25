tensordecoders_sources = [
  'gsttensordecoders.c',
  'gstssdobjectdetector.c'
]

gsttensordecoders = library('gsttensordecoders',
  tensordecoders_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep, gstanalytics_dep, gio_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gsttensordecoders]
