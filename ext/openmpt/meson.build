openmpt_dep = dependency('libopenmpt', required: get_option('openmpt'))
if openmpt_dep.found()
  gstopenmpt = library('gstopenmpt',
    'gstopenmptdec.c', 'plugin.c',
    c_args: gst_plugins_bad_args,
    include_directories: [configinc],
    dependencies: [gstbadaudio_dep, gstaudio_dep, openmpt_dep],
    install: true,
    install_dir: plugins_install_dir,
  )
  pkgconfig.generate(gstopenmpt, install_dir: plugins_pkgconfig_install_dir)
endif
