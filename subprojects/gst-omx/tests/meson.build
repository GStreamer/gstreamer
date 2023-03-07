# FIXME: make check work on windows
if get_option('tests').disabled() or static_build or host_machine.system() == 'windows'
  subdir_done()
endif

if gstcheck_dep.found()
  subdir('check')
endif