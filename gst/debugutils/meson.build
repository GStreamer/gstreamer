debugutilsbad_sources = [
  'gstdebugspy.c',
  'gsterrorignore.c',
  'debugutilsbad.c',
  'fpsdisplaysink.c',
  'gstchecksumsink.c',
  'gstchopmydata.c',
  'gstcompare.c',
  'gstfakevideosink.c',
  'gstwatchdog.c',
]

gstdebugutilsbad = library('gstdebugutilsbad',
  debugutilsbad_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep],
  install : true,
  install_dir : plugins_install_dir,
)
