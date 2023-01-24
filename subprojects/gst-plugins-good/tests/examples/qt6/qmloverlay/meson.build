sources = [
  'main.cpp',
]

qt_preprocessed = qt6_mod.preprocess(qresources : 'qmloverlay.qrc')
executable('qml6gloverlay', sources, qt_preprocessed,
    dependencies : [gst_dep, qt6qml_example_deps],
    override_options : ['cpp_std=c++17'],
    c_args : gst_plugins_good_args,
    include_directories : [configinc],
    install: false)
