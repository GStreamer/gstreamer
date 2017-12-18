ladspa_sources = [
  'gstladspa.c',
  'gstladspafilter.c',
  'gstladspasource.c',
  'gstladspasink.c',
  'gstladspautils.c',
]

ladspa_dep = dependency('lrdf', required : false)
if ladspa_dep.found()
  gstrsvg = library('gstladspa',
    ladspa_sources,
    c_args : gst_plugins_bad_args,
    link_args : noseh_link_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstaudio_dep, gstbase_dep, gmodule_dep, ladspa_dep, mathlib],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
