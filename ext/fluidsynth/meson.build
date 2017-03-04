fluidsynth_dep = dependency('fluidsynth', version : '>= 1.0', required : false)

if fluidsynth_dep.found()
  gstfluidsynth = library('gstfluidsynthmidi',
    'gstfluiddec.c',
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstaudio_dep, gst_dep, fluidsynth_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
