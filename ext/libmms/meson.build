mms_dep = dependency('libmms', version : '>= 0.4', required : false)

if mms_dep.found()
  gstmms = library('gstmms',
    'gstmms.c',
    c_args : gst_plugins_bad_args,
    link_args : noseh_link_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep, mms_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
