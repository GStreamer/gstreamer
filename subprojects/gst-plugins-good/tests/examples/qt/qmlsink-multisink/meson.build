sources = [
  'videoitem/videoitem.cpp',
  'main.cpp'
]

qt_preprocessed = qt5_mod.preprocess (qresources : 'qmlsink-multi.qrc',
                                      moc_headers : 'videoitem/videoitem.h')
executable('qmlsink-multisink', sources, qt_preprocessed,
    dependencies : [gst_dep, gstgl_dep, qt5qml_example_deps],
    override_options : ['cpp_std=c++11'],
    c_args : gst_plugins_good_args,
    include_directories : [configinc],
    install: false)
