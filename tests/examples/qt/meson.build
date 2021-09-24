if qt5_option.disabled()
  subdir_done()
endif

# We already did all the checks when building the qt plugin
if not qt5qml_dep.found()
  subdir_done()
endif

qt5qml_example_deps = dependency('qt5', modules : ['Core', 'Gui', 'Widgets', 'Qml', 'Quick'],
                         required: get_option('examples'))

if not qt5qml_example_deps.found()
  subdir_done()
endif

subdir('qmloverlay')
subdir('qmlsink')
subdir('qmlsink-multisink')
subdir('qmlsink-dynamically-added')
subdir('qmlsrc')
