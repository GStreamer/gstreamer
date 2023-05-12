if qt6_option.disabled()
  subdir_done()
endif

# We already did all the checks when building the qt6 plugin
if not qt6qml_dep.found()
  subdir_done()
endif

qt6qml_example_deps = dependency('qt6', modules : ['Core', 'Gui', 'Widgets', 'Qml', 'Quick'],
                         required: get_option('examples'))

if not qt6qml_example_deps.found()
  subdir_done()
endif

subdir('qmlsink')
subdir('qmlsrc')
subdir('qmloverlay')
subdir('qmlmixer')
