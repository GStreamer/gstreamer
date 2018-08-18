if host_machine.system() != 'windows'
  if not get_option('tests').disabled() and gstcheck_dep.found()
    subdir('check')
  endif
endif
