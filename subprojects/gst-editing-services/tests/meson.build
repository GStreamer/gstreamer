if get_option('tests').disabled() or static_build
  subdir_done()
endif

# FIXME: make check work on windows
if host_system != 'windows' and gstcheck_dep.found()
  subdir('check')
  subdir('validate')
endif

subdir('benchmarks')
