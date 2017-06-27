aom_dep = dependency('aom', required: false)
if aom_dep.found()
  gstaom = library('gstaom',
    ['gstaom.c', 'gstav1enc.c', 'gstav1dec.c'],
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstpbutils_dep, gstvideo_dep, aom_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
