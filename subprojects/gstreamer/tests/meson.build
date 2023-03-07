if get_option('tests').disabled() or static_build
  subdir_done()
endif

if not get_option('benchmarks').disabled()
  subdir('benchmarks')
endif
if gst_check_dep.found()
  subdir('validate')
  subdir('check')
endif
if not get_option('examples').disabled()
  subdir('examples')
endif
if not get_option('tools').disabled()
  subdir('misc')
endif
