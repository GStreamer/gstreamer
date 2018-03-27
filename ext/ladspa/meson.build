ladspa_sources = [
  'gstladspa.c',
  'gstladspafilter.c',
  'gstladspasource.c',
  'gstladspasink.c',
  'gstladspautils.c',
]

ladspa_cargs = []

lrdf_dep = dependency('lrdf', required : false)
if lrdf_dep.found()
  ladspa_cargs = ['-DHAVE_LRDF']
endif

if cc.has_header('ladspa.h')
  gstladspa = library('gstladspa',
    ladspa_sources,
    c_args : gst_plugins_bad_args + ladspa_cargs,
    link_args : noseh_link_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstaudio_dep, gstbase_dep, gmodule_dep, lrdf_dep, mathlib],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
