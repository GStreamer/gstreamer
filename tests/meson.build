# FIXME: make check work on windows
if host_machine.system() != 'windows'
  if not get_option('tests').disabled() and gst_check_dep.found()
    subdir('check')
  endif
endif
if not get_option('examples').disabled()
  subdir('examples')
endif
#subdir('files')
subdir('icles')
