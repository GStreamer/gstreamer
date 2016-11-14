fbdevsink_sources = [
  'gstfbdevsink.c',
]

if cc.has_header('linux/fb.h', required : false)
  gstfbdevsink = library('gstfbdevsink',
    fbdevsink_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep, gstvideo_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
