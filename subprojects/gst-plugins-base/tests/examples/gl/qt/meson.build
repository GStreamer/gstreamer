qt_cxx_warn_less = cxx.get_supported_arguments(['-Wno-aggregate-return'])

subdir('videooverlay')

if not qt5_have_tools
  subdir_done()
endif

subdir('mousevideooverlay')

gl_option = get_option('gl')
qt5gl_option = qt5_option.disable_auto_if(not gstgl_dep.found())
qt5opengl_dep = dependency('qt5', modules: ['OpenGL'], method: qt5_method,
    required: qt5gl_option.enabled() and gl_option.enabled() and examples_option.enabled())
if qt5opengl_dep.found()
  subdir('qglwidgetvideooverlay')
  subdir('qglwtextureshare')
endif
