pluginsdirs = [  ]
if gst_dep.type_name() == 'pkgconfig'
  pbase = dependency('gstreamer-plugins-base-' + apiversion)

  pluginsdirs = [gst_dep.get_variable('pluginsdir'),
                 pbase.get_variable('pluginsdir')]
  gst_plugin_scanner_dir = gst_dep.get_variable('pluginscannerdir')
else
  gst_plugin_scanner_dir = subproject('gstreamer').get_variable('gst_scanner_dir')
endif
gst_plugin_scanner_path = join_paths(gst_plugin_scanner_dir, 'gst-plugin-scanner')

if get_option('tests').disabled() or static_build
  subdir_done()
endif

# FIXME: make check work on windows
if host_machine.system() != 'windows' and gst_check_dep.found()
  subdir('check')
endif

subdir('launcher_tests')
