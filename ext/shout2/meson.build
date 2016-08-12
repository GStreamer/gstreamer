shout2_sources = ['gstshout2.c']

shout2_dep = dependency('shout', version : '>= 2.0', required : false)

if shout2_dep.found()
  gstshout2 = library('gstshout2',
    shout2_sources,
    c_args : gst_plugins_good_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstbase_dep, shout2_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
