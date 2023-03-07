if get_option('examples').disabled() or static_build or host_machine.system() == 'windows'
  subdir_done()
endif

if gstgl_dep.found()
  subdir('egl')
endif
