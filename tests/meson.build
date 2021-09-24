# FIXME: make check work on windows
if host_machine.system() != 'windows' and gstcheck_dep.found()
  subdir('check')
endif

subdir('validate')
subdir('benchmarks')