fdkaac_dep = dependency('fdk-aac', required : false)

if fdkaac_dep.found()
  fdkaac_defines = []

  # Enable 7.1 support
  if fdkaac_dep.version().version_compare('>=0.1.4')
    fdkaac_defines += ['-DHAVE_FDK_AAC_0_1_4']
  endif

  gstfdkaac = library('gstfdkaac',
    ['plugin.c', 'gstfdkaacenc.c', 'gstfdkaacdec.c'],
    c_args : gst_plugins_bad_args + fdkaac_defines,
    include_directories : [configinc],
    dependencies : [gstaudio_dep, gstpbutils_dep, fdkaac_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
