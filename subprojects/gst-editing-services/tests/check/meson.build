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
    ['ges/mixers'],
    ['ges/group'],
    ['ges/project'],
    ['ges/track'],
    ['ges/tempochange'],
    ['ges/negative'],
    ['ges/markerlist'],
    ['nle/simple'],
    ['nle/complex'],
    ['nle/nleoperation'],
    ['nle/nlecomposition'],
    ['nle/tempochange']
]

fsmod = import('fs')
test_defines = [
  '-UG_DISABLE_ASSERT',
  '-UG_DISABLE_CAST_CHECKS',
  '-DGES_TEST_FILES_PATH="@0@"'.format(fsmod.as_posix(join_paths(meson.current_source_dir(), 'assets'))),
  '-DGST_CHECK_TEST_ENVIRONMENT_BEACON="GST_STATE_IGNORE_ELEMENTS"',
  '-DTESTFILE="' + fsmod.as_posix(meson.current_source_dir()) + '/meson.build"',
  '-DGST_USE_UNSTABLE_API',
]

pluginsdirs = []
if gst_dep.type_name() == 'pkgconfig'
  pbase = dependency('gstreamer-plugins-base-' + apiversion, required : false)
  pbad = dependency('gstreamer-plugins-bad-' + apiversion, required : false)

  pluginsdirs = [gst_dep.get_variable('pluginsdir'),
                 pbase.get_variable('pluginsdir'),
                 pbad.get_variable('pluginsdir')]
 gst_plugin_scanner_dir = gst_dep.get_variable('pluginscannerdir')
else
 gst_plugin_scanner_dir = subproject('gstreamer').get_variable('gst_scanner_dir')
endif
gst_plugin_scanner_path = join_paths(gst_plugin_scanner_dir, 'gst-plugin-scanner')

foreach t : ges_tests
  fname = '@0@.c'.format(t.get(0))
  test_name = t.get(0).underscorify()
  if t.length() == 2
    skip_test = t.get(1)
  else
    skip_test = false
  endif

  if not skip_test
    env = environment()
    env.set('GST_PLUGIN_SYSTEM_PATH_1_0', '')
    env.set('GST_STATE_IGNORE_ELEMENTS', '')
    env.set('CK_DEFAULT_TIMEOUT', '20')
    env.set('GST_REGISTRY', '@0@/@1@.registry'.format(meson.current_build_dir(), test_name))
    env.set('GST_PLUGIN_PATH_1_0', [meson.global_build_root()] + pluginsdirs)

    exe = executable(test_name, fname,
        'ges/test-utils.c', 'nle/common.c',
        c_args : ges_c_args + test_defines,
        include_directories : [configinc],
        dependencies : libges_deps + [gstcheck_dep, ges_dep],
    )
    test(test_name, exe, env: env, timeout : 3 * 60)
  endif
endforeach

if gstvalidate_dep.found() and not get_option('tools').disabled()
  # filename: is .validatetest
  scenarios = {
    'check_video_track_restriction_scale': false,
    'check_video_track_restriction_scale_with_keyframes': false,
    'check_edit_in_frames': false,
    'check_edit_in_frames_with_framerate_mismatch': false,
    'check_layer_activness_gaps': false,
    'seek_with_stop': true,
    'seek_with_stop.check_clock_sync': true,
    'edit_while_seeked_with_stop': true,
    'complex_effect_bin_desc': true,
    'check_keyframes_in_compositor_two_sources': true,
    'check-clip-positioning': true,
    'set-layer-on-command-line': true,
  }

  foreach scenario, is_validatetest: scenarios

    env = environment()
    env.set('GST_PLUGIN_SYSTEM_PATH_1_0', '')
    env.set('GST_STATE_IGNORE_ELEMENTS', '')
    env.set('CK_DEFAULT_TIMEOUT', '20')
    env.set('GST_REGISTRY', '@0@/@1@.registry'.format(meson.current_build_dir(), 'scenarios'))
    env.set('GST_PLUGIN_PATH_1_0', [meson.global_build_root()] + pluginsdirs)
    env.set('GST_VALIDATE_LOGSDIR', meson.current_build_dir() / scenario)
    env.set('GST_PLUGIN_SCANNER_1_0', gst_plugin_scanner_path)

    if is_validatetest
      testfile = meson.current_source_dir() / 'scenarios' / scenario + '.validatetest'
      test(scenario, ges_launch, env: env, args: ['--no-interactive', '--set-test-file', testfile, '--mute'])
    else
      scenario_file = meson.current_source_dir() / 'scenarios' / scenario + '.scenario'
      test(scenario, ges_launch, env: env, args: ['--no-interactive', '--set-scenario', scenario_file])
    endif

  endforeach
  test('simple_playback_test', ges_launch, env: env, args: ['+test-clip', 'blue', 'd=0.1', '--videosink=fakevideosink', '--audiosink=fakeaudiosink'])
endif

if build_gir
  # Make sure to use the subproject gst-validate-launcher if available.
  if gstvalidate_dep.found() and gstvalidate_dep.type_name() == 'internal'
    runtests = subproject('gst-devtools').get_variable('launcher')
  else
    runtests = find_program('gst-validate-launcher', required : false)
  endif

  if runtests.found()
    env = environment()
    env.set('GST_PLUGIN_SYSTEM_PATH_1_0', '')
    env.set('GST_STATE_IGNORE_ELEMENTS', '')
    env.set('CK_DEFAULT_TIMEOUT', '20')
    env.set('GST_REGISTRY', '@0@/@1@.registry'.format(meson.current_build_dir(), 'scenarios'))
    env.set('GST_PLUGIN_PATH_1_0', [meson.global_build_root()] + pluginsdirs)
    env.set('GI_TYPELIB_PATH', meson.current_build_dir() / '..' / '..' / 'ges')

    test('pythontests', runtests, args: ['--pyunittest-dir', meson.current_source_dir(), 'pyunittest', '--dump-on-failure'],
         env: env)
  endif
endif
