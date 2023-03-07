if get_option('tests').disabled() or static_build
  subdir_done()
endif

if gstcheck_dep.found()
  subdir('check')
endif
