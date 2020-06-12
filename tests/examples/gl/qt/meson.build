qt5_mod = import('qt5')

qt5gui_dep = dependency('qt5', modules : ['Core', 'Gui', 'Widgets'], required : false)
qt5opengl_dep = dependency('qt5', modules : ['OpenGL'], required : false)
# FIXME: other platforms
libgl = cc.find_library ('GL', required : false)

if qt5gui_dep.found()
  subdir('videooverlay')
  if libgl.found()
    subdir('mousevideooverlay')
    if qt5opengl_dep.found()
      subdir('qglwidgetvideooverlay')
      subdir('qglwtextureshare')
    endif
  endif
endif
