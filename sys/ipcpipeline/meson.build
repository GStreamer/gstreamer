ipcpipeline_sources = [
  'gstipcpipeline.c',
  'gstipcpipelinecomm.c',
  'gstipcpipelinesink.c',
  'gstipcpipelinesrc.c',
  'gstipcslavepipeline.c'
]

if get_option('ipcpipeline').disabled()
  subdir_done()
endif

gstipcpipeline = library('gstipcpipeline',
  ipcpipeline_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep],
  install : true,
  install_dir : plugins_install_dir,
)
pkgconfig.generate(gstipcpipeline, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstipcpipeline]
