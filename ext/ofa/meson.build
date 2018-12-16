ofa_dep = dependency('libofa', version: '>= 0.9.3', required: get_option('ofa'))

if ofa_dep.found()
  gstofa = library('gstofa', 'gstofa.c',
    c_args: gst_plugins_bad_args,
    include_directories: [configinc],
    dependencies: [gstaudio_dep, ofa_dep],
    install: true,
    install_dir: plugins_install_dir,
  )
  pkgconfig.generate(gstofa, install_dir: plugins_pkgconfig_install_dir)
endif
