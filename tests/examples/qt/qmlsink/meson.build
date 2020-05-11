sources = [
  'main.cpp',
]

qt_preprocessed = qt5_mod.preprocess(qresources : 'qmlsink.qrc')
executable('qmlsink', sources, qt_preprocessed,
    dependencies : [gst_dep, qt5qml_example_deps],
    c_args : gst_plugins_good_args,
    include_directories : [configinc],
    install: false)
