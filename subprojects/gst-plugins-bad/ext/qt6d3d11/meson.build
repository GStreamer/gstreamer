gstqt6d3d11_sources = [
  'gstqml6d3d11sink.cpp',
  'gstqsg6d3d11node.cpp',
  'gstqt6d3d11videoitem.cpp',
  'plugin.cpp',
]

gstqt6d3d11_headers = [
  'gstqml6d3d11sink.h',
  'gstqsg6d3d11node.h',
  'gstqt6d3d11videoitem.h',
]

doc_sources = []
foreach s: gstqt6d3d11_sources + gstqt6d3d11_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'qt6d3d11': pathsep.join(doc_sources)
}

moc_headers = [
  'gstqsg6d3d11node.h',
  'gstqt6d3d11videoitem.h'
]

have_qt6d3d11 = false

qt6_method = get_option('qt-method')
qt6_option = get_option('qt6d3d11')
if qt6_option.disabled()
  subdir_done()
endif

if not gstd3d11_dep.found()
  if qt6_option.enabled()
    error('qt6 plugin was enabled explicitly, but required d3d11 dep was not found')
  else
    subdir_done()
  endif
endif

qt6_mod = import('qt6')
if meson.version().version_compare('>=1.6')
  have_tools = qt6_mod.has_tools(method: qt6_method, tools: ['moc', 'uic', 'rcc'])
else
  have_tools = qt6_mod.has_tools(method: qt6_method)
endif
if not have_tools
  if qt6_option.enabled()
    error('qt6 d3d11 plugin was enabled, but qt specific tools were not found')
  endif
  subdir_done()
endif

qt6qml_dep = dependency('qt6', modules : ['Core', 'Gui', 'Qml', 'Quick'],
                        method: qt6_method, required: qt6_option)
if not qt6qml_dep.found()
  subdir_done()
endif

have_qt6d3d11 = true
moc_files = qt6_mod.preprocess(moc_headers : moc_headers, method: qt6_method)
gstqt6d3d11 = library('gstqt6d3d11', gstqt6d3d11_sources, moc_files,
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
  cpp_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories: [configinc, libsinc],
  dependencies : [gst_dep, gstvideo_dep, gstd3d11_dep, qt6qml_dep],
  override_options : ['cpp_std=c++17'],
  install: true,
  install_dir : plugins_install_dir
)
plugins += [gstqt6d3d11]
