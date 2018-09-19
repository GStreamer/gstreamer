clutter_dep = dependency('clutter-1.0', version: '>= 1.8', required: get_option('examples'))
if clutter_dep.found()
  # works on win32 and X
  executable('cluttershare', 'cluttershare.c',
    dependencies : [gstgl_dep, gl_dep, video_dep, clutter_dep],
    install: false)

  clutter_glx_dep = dependency('clutter-glx-1.0', version: '>= 1.8', required: get_option('examples'))
  clutter_x11_dep = dependency('clutter-x11-1.0', version: '>= 1.8', required: get_option('examples'))
  xcomposite_dep = dependency('xcomposite', required: get_option('examples'))

  if clutter_glx_dep.found() and clutter_x11_dep.found() and xcomposite_dep.found()
    executable('clutteractor', 'clutteractor.c',
      dependencies : [gstgl_dep, gl_dep, video_dep, clutter_dep, clutter_glx_dep, clutter_x11_dep, xcomposite_dep],
      install: false)
    executable('clutteractortee', 'clutteractortee.c',
      dependencies : [gstgl_dep, gl_dep, video_dep, clutter_dep, clutter_glx_dep, clutter_x11_dep, xcomposite_dep],
      install: false)
  endif
endif
