# FIXME: something is wrong with plugin paths / whitelisting here
pluginsdirs = []
if gst_dep.type_name() == 'pkgconfig'
  pbase = dependency('gstreamer-plugins-base-' + api_version, required : false)
  pbad = dependency('gstreamer-plugins-bad-' + api_version, required : false)

  pluginsdirs = [gst_dep.get_pkgconfig_variable('pluginsdir'),
                 pbase.get_pkgconfig_variable('pluginsdir'),
                 pbad.get_pkgconfig_variable('pluginsdir')]
endif

test_c_args = [
  '-UG_DISABLE_ASSERT',
  '-UG_DISABLE_CAST_CHECKS',
  '-DGST_CHECK_TEST_ENVIRONMENT_BEACON="GST_PLUGIN_LOADING_WHITELIST"',
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
]

if not get_option('rtspclientsink').disabled()
  rtsp_server_tests += ['gst/rtspclientsink']
endif

foreach test_name : rtsp_server_tests
    env = environment()
    env.set('GST_PLUGIN_SYSTEM_PATH_1_0', '')
    env.set('GST_STATE_IGNORE_ELEMENTS', '')
    env.set('GST_PLUGIN_LOADING_WHITELIST', 'gstreamer:gst-plugins-base:gst-plugins-good:gst-plugins-bad:gst-rtsp-server')
    env.set('CK_DEFAULT_TIMEOUT', '120')
    env.set('GST_REGISTRY', '@0@/@1@.registry'.format(meson.current_build_dir(), test_name))
    env.set('GST_PLUGIN_PATH_1_0', [meson.build_root()] + pluginsdirs)

  fname = '@0@.c'.format(test_name)
  test_name = test_name.underscorify()
  exe = executable(test_name, fname,
    include_directories : rtspserver_incs,
    c_args : rtspserver_args + test_c_args,
    dependencies : [gstcheck_dep, gstrtsp_dep, gstrtp_dep, gst_rtsp_server_dep]
  )
  test(test_name, exe,
    env : env,
    timeout : 120,
    is_parallel: false
  )
endforeach
