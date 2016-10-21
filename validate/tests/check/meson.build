gstcheck_dep = dependency('gstreamer-check-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_check_dep'])

# tests and condition when to skip the test
validate_tests = [
  ['validate/padmonitor'],
  ['validate/monitoring'],
  ['validate/reporting'],
  ['validate/overrides']
]

test_defines = [
  '-UG_DISABLE_ASSERT',
  '-UG_DISABLE_CAST_CHECKS',
  '-DGST_CHECK_TEST_ENVIRONMENT_BEACON="GST_STATE_IGNORE_ELEMENTS"',
  '-DTESTFILE="' + meson.current_source_dir() + '/meson.build"',
  '-DGST_USE_UNSTABLE_API',
]

runcmd = run_command(getpluginsdir, 'gstreamer', 'gstreamer-' + apiversion)
if runcmd.returncode() == 0
    needed_plugins_dirs = runcmd.stdout().strip()
    message('Using GStreamer plug-ins in ' + needed_plugins_dirs)
else
    error('Could not determine GStreamer core plugins directory for unit tests.')
endif

test_env = [
    'GST_PLUGIN_SYSTEM_PATH_1_0=',
    'GST_PLUGIN_PATH_1_0=' + needed_plugins_dirs,
    'GST_PLUGIN_SCANNER_1_0='+ meson.build_root() + '/libs/gst/helpers/gst-plugin-scanner',
    'GST_STATE_IGNORE_ELEMENTS=',
    'CK_DEFAULT_TIMEOUT=20',
]

foreach t : validate_tests
  test_name = t.get(0)
  if t.length() == 2
    skip_test = t.get(1)
  else
    skip_test = false
  endif

  if not skip_test
    exe = executable(test_name, '@0@.c'.format(test_name),
        'validate/test-utils.c',
        c_args : gst_c_args + test_defines,
        include_directories : [inc_dirs],
        dependencies : [validate_dep, gstcheck_dep],
    )
    test(test_name, exe,
      env: test_env + ['GST_REGISTRY=@0@/@1@.registry'.format(meson.current_build_dir(), test_name)]
    )
  endif
endforeach

