sources = [
  'main.cpp',
]

qt_preprocessed = qt5_mod.preprocess(qresources : 'qmlsrc.qrc')
executable('qmlsrc', sources, qt_preprocessed,
    dependencies : [gst_dep, qt5qml_example_deps],
    override_options : ['cpp_std=c++11'],
    c_args : gst_plugins_good_args,
    include_directories : [configinc],
    install: false)
