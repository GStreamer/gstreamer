dssim_dep = dependency('dssim', required : false,
    fallback: ['dssim', 'dssim_dep'])

if dssim_dep.found()
  gstiqa = library('gstiqa',
    'iqa.c',
    c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API', '-DHAVE_DSSIM'],
    include_directories : [configinc],
    dependencies : [gst_dep, gstbadvideo_dep, gstbadbase_dep, dssim_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif

