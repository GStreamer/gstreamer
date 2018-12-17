sndfile_dep = dependency('sndfile', version: '>= 1.0.16', required: get_option('sndfile'))

if sndfile_dep.found()
  gstsndfile = library('gstsndfile',
    'gstsf.c', 'gstsfdec.c', # 'gstsfsink.c', 'gstsfsrc.c',
    c_args: gst_plugins_bad_args,
    include_directories: [configinc, libsinc],
    dependencies: [gstaudio_dep, gst_dep, sndfile_dep],
    install: true,
    install_dir: plugins_install_dir,
  )
  pkgconfig.generate(gstsndfile, install_dir: plugins_pkgconfig_install_dir)
endif
