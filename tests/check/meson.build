# tests and condition when to skip the test
ges_tests = [
    ['ges/asset'],
    ['ges/backgroundsource'],
    ['ges/basic'],
    ['ges/layer'],
    ['ges/effects'],
    ['ges/uriclip'],
    ['ges/clip'],
    ['ges/timelineedition'],
    ['ges/titles'],
    ['ges/transition'],
    ['ges/overlays'],
    ['ges/text_properties'],
    ['ges/mixers'],
    ['ges/group'],
    ['ges/project'],
    ['ges/track'],
    ['ges/tempochange'],
    ['nle/simple'],
    ['nle/complex'],
    ['nle/nleoperation'],
    ['nle/nlecomposition'],
    ['nle/tempochange']
]

test_defines = [
  '-UG_DISABLE_ASSERT',
  '-UG_DISABLE_CAST_CHECKS',
  '-DGST_CHECK_TEST_ENVIRONMENT_BEACON="GST_STATE_IGNORE_ELEMENTS"',
  '-DTESTFILE="' + meson.current_source_dir() + '/meson.build"',
  '-DGST_USE_UNSTABLE_API',
]

runcmd = run_command('pkg-config', '--variable=pluginsdir', 'gstreamer-1.0',
        'gstreamer-plugins-base-1.0', 'gstreamer-plugins-bad-1.0')
if runcmd.returncode() == 0
    needed_plugins_dirs = meson.build_root() + 'plugins/nle'
    foreach path: runcmd.stdout().strip().split(' ')
        needed_plugins_dirs = needed_plugins_dirs + ':' + path
    endforeach
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

foreach t : ges_tests
  test_name = t.get(0)
  if t.length() == 2
    skip_test = t.get(1)
  else
    skip_test = false
  endif

  if not skip_test
    exe = executable(test_name, '@0@.c'.format(test_name),
        'ges/test-utils.c', 'nle/common.c',
        c_args : ges_c_args + test_defines,
        include_directories : [configinc],
        dependencies : libges_deps + [gstcheck_dep, ges_dep],
    )
    test(test_name, exe,
      env: test_env + ['GST_REGISTRY=@0@/@1@.registry'.format(meson.current_build_dir(), test_name)]
    )
  endif
endforeach
