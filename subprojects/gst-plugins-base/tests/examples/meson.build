subdir('app')
subdir('audio')
subdir('compositor')
subdir('device-provider')
subdir('dynamic')
subdir('decodebin_next')
subdir('encoding')
subdir('fft')
subdir('gio')

# Qt is used by gl/qt and overlay examples
examples_option = get_option('examples')
qt5_method = get_option('qt-method')
qt5_option = get_option('qt5').require(have_cxx)
qt5_option = qt5_option.disable_auto_if(examples_option.disabled())

qt5gui_dep = dependency('', required: false)
qt5opengl_dep = dependency('', required: false)
qt5_have_tools = false
qt5_mod = import('qt5')
if qt5_option.allowed()
  qt5gui_dep = dependency('qt5', modules: ['Core', 'Gui', 'Widgets'], method: qt5_method,
      required: qt5_option.enabled() and examples_option.enabled())
  if meson.version().version_compare('>=1.6')
    qt5_have_tools = qt5_mod.has_tools(method: qt5_method, tools: ['moc', 'uic', 'rcc'],
        required: qt5_option.enabled() and examples_option.enabled())
  else
    qt5_have_tools = qt5_mod.has_tools(method: qt5_method,
        required: qt5_option.enabled() and examples_option.enabled())
  endif
endif

if gstgl_dep.found()
  subdir('gl')
endif
subdir('overlay')

subdir('overlaycomposition')
subdir('playback')
subdir('playrec')
subdir('seek')
subdir('snapshot')
