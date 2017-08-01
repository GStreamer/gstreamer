ipcpipeline_sources = [
  'gstipcpipeline.c',
  'gstipcpipelinecomm.c',
  'gstipcpipelinesink.c',
  'gstipcpipelinesrc.c',
  'gstipcslavepipeline.c'
]

if cc.has_header ('sys/socket.h') and cc.has_function ('pipe') and cc.has_function ('socketpair')

  gstipcpipeline = library('gstipcpipeline',
    ipcpipeline_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep],
    install : true,
    install_dir : plugins_install_dir,
  )

endif
