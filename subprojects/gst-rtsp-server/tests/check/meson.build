pluginsdirs = []
if gst_dep.type_name() == 'pkgconfig'
  pbase = dependency('gstreamer-plugins-base-' + api_version, required: true)
  pbad = dependency('gstreamer-plugins-bad-' + api_version, required: true)

  pluginsdirs = [gst_dep.get_variable('pluginsdir'),
                 pbase.get_variable('pluginsdir'),
                 pbad.get_variable('pluginsdir')]

  gst_plugin_scanner_dir = gst_dep.get_variable('pluginscannerdir')
else
  gst_plugin_scanner_dir = subproject('gstreamer').get_variable('gst_scanner_dir')
endif
gst_plugin_scanner_path = join_paths(gst_plugin_scanner_dir, 'gst-plugin-scanner')

fsmod = import('fs')
test_c_args = [
  '-UG_DISABLE_ASSERT',
  '-UG_DISABLE_CAST_CHECKS',
  '-DGST_CHECK_TEST_ENVIRONMENT_BEACON="GST_PLUGIN_LOADING_WHITELIST"',
  '-DGST_TEST_FILES_PATH="' + fsmod.as_posix(meson.current_source_dir()) + '/../files"',
]

rtsp_server_tests = [
  'gst/addresspool',
  'gst/client',
  'gst/mountpoints',
  'gst/mediafactory',
  'gst/media',
  'gst/permissions',
  'gst/rtspserver',
  'gst/sessionmedia',
  'gst/sessionpool',
  'gst/stream',
  'gst/threadpool',
  'gst/token',
  'gst/onvif',
]

if not get_option('rtspclientsink').disabled()
  rtsp_server_tests += ['gst/rtspclientsink']
endif

foreach test_name : rtsp_server_tests
  fname = '@0@.c'.format(test_name)
  test_name = test_name.underscorify()

  env = environment()
  env.set('GST_PLUGIN_SYSTEM_PATH_1_0', '')
  env.set('GST_STATE_IGNORE_ELEMENTS', '')
  env.set('GST_PLUGIN_LOADING_WHITELIST', 'gstreamer:gst-plugins-base:gst-plugins-good:gst-plugins-bad:gst-rtsp-server@' + meson.project_build_root())
  env.set('CK_DEFAULT_TIMEOUT', '120')
  env.set('GST_REGISTRY', join_paths(meson.current_build_dir(), '@0@.registry'.format(test_name)))
  env.set('GST_PLUGIN_PATH_1_0', [meson.global_build_root()] + pluginsdirs)
  env.set('GST_PLUGIN_SCANNER_1_0', gst_plugin_scanner_path)

  exe = executable(test_name, fname,
    include_directories : rtspserver_incs,
    c_args : rtspserver_args + test_c_args,
    dependencies : [gstcheck_dep, gstrtsp_dep, gstrtp_dep, gst_rtsp_server_dep,
      gstvideo_dep]
  )
  test(test_name, exe,
    env : env,
    timeout : 120,
    is_parallel: false
  )
endforeach
