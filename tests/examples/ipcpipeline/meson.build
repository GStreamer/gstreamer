if get_option('ipcpipeline').disabled() or not build_ipcpipeline
  subdir_done()
endif

executable('ipcpipeline1', 'ipcpipeline1.c',
  include_directories: [configinc],
  dependencies: [glib_dep, gst_dep, gstbase_dep],
  c_args: gst_plugins_bad_args,
  install: false)

executable('ipc-play', 'ipc-play.c',
  include_directories: [configinc],
  dependencies: [glib_dep, gst_dep, gstbase_dep, gstvideo_dep],
  c_args: gst_plugins_bad_args,
  install: false)
