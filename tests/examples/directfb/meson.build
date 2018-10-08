if directfb_dep.found()
  executable('gstdfb', 'gstdfb.c',
    include_directories : [configinc],
    dependencies : [gst_dep, directfb_dep, libm],
    c_args : gst_plugins_bad_args,
    install: false)
endif
