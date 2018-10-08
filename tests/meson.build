# FIXME: make check work on windows
if host_system != 'windows'
  if not get_option('tests').disabled() and gstcheck_dep.found()
    subdir('check')
  endif
endif
if not get_option('examples').disabled()
  subdir('examples')
  subdir('icles')
endif
