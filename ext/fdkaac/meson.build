fdkaac_dep = dependency('fdk-aac', required : false)

if fdkaac_dep.found()
  gstfdkaac = library('gstfdkaac',
    ['plugin.c', 'gstfdkaacenc.c', 'gstfdkaacdec.c'],
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstaudio_dep, gstpbutils_dep, fdkaac_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
