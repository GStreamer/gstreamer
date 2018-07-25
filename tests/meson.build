# FIXME: make check work on windows
if host_machine.system() != 'windows'
  subdir('check')
endif
if not get_option('examples').disabled()
  subdir('examples')
endif
