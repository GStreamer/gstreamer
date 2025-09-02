if get_option('inter').disabled()
  subdir_done()
endif

executable('intertest', 'gstintertest.c',
           include_directories: [configinc],
           dependencies: [gst_dep],
           c_args: gst_plugins_bad_args,
           install: false)
