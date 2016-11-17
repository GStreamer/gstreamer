# FIXME: something is wrong with plugin paths / whitelisting here
gstcheck_dep = dependency('gstreamer-check-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_check_dep'])

test_plugins_modules = [
  ['gstreamer-1.0', true],
  ['gstreamer-plugins-base-1.0', true],
  ['gstreamer-plugins-good-1.0', false],
  ['gstreamer-plugins-bad-1.0', false],
]
test_plugin_path = ''
foreach m : test_plugins_modules
  m_name = m[0]
  m_required = m[1]
  runcmd = run_command('pkg-config', '--variable=pluginsdir', m_name + ' ' + gst_req)
  if runcmd.returncode() == 0
    module_plugin_path = runcmd.stdout().strip()
    message('Using '+ m_name + ' plugins in ' + module_plugin_path)
    if test_plugin_path == ''
      test_plugin_path = module_plugin_path
    elif module_plugin_path != test_plugin_path
      test_plugin_path = test_plugin_path + ':' + module_plugin_path
    endif
  elif m_required
    error('Could not determine ' + m_name + ' plugins directory for unit tests.')
  endif
endforeach

test_c_args = [
  '-UG_DISABLE_ASSERT',
  '-UG_DISABLE_CAST_CHECKS',
  '-DGST_CHECK_TEST_ENVIRONMENT_BEACON="GST_PLUGIN_LOADING_WHITELIST"',
]

test_env = [
  'GST_PLUGIN_SYSTEM_PATH_1_0=',
  'GST_PLUGIN_PATH_1_0=' + meson.build_root() + '/gst/rtsp-sink:' + test_plugin_path,
  'GST_PLUGIN_LOADING_WHITELIST=gstreamer:gst-plugins-base:gst-plugins-good:gst-plugins-bad:gst-rtsp-server',
  'CK_DEFAULT_TIMEOUT=120',
]

rtsp_server_tests = [
  'gst/addresspool',
  'gst/client',
  'gst/mountpoints',
  'gst/mediafactory',
  'gst/media',
  'gst/permissions',
  'gst/rtspserver',
  'gst/rtspclientsink',
  'gst/sessionmedia',
  'gst/sessionpool',
  'gst/stream',
  'gst/threadpool',
  'gst/token',
]

all_env = ''
foreach e : test_env
  all_env = all_env + ' ' + e
endforeach
message('test env:' + all_env)

foreach test_name : rtsp_server_tests
  exe = executable(test_name, '@0@.c'.format(test_name),
    include_directories : rtspserver_incs,
    c_args : rtspserver_args + test_c_args,
    dependencies : [gstcheck_dep, gstrtsp_dep, gstrtp_dep, gst_rtsp_server_dep]
  )
  test(test_name, exe,
    env : test_env + [ 'GST_REGISTRY=@0@/@1@.registry'.format(meson.current_build_dir(), test_name) ],
    timeout : 120,
  )
endforeach
