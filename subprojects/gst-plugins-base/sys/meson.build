if x11_dep.found()
  # XShm is only used by ximage and xvimage
  # FIXME: Need to check for XShmAttach inside libXext
  xshm_dep = dependency('xext', required : get_option('xshm'))
  core_conf.set('HAVE_XSHM', xshm_dep.found())

  subdir('ximage')
  subdir('xvimage')
endif
