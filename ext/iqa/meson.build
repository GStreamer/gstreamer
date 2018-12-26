dssim_dep = dependency('dssim', required : get_option('iqa'),
    fallback: ['dssim', 'dssim_dep'])

if dssim_dep.found()
  gstiqa = library('gstiqa',
    'iqa.c',
    c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API', '-DHAVE_DSSIM'],
    include_directories : [configinc],
    dependencies : [gstvideo_dep, gstbase_dep, gst_dep, dssim_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstiqa, install_dir : plugins_pkgconfig_install_dir)
endif

