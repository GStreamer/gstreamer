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
  '-DGES_TEST_FILES_PATH="' + meson.current_source_dir() + '/ges/"',
  '-DGST_CHECK_TEST_ENVIRONMENT_BEACON="GST_STATE_IGNORE_ELEMENTS"',
  '-DTESTFILE="' + meson.current_source_dir() + '/meson.build"',
  '-DGST_USE_UNSTABLE_API',
]

pluginsdirs = []
if not meson.is_subproject()
  pkgconfig = find_program('pkg-config')
  runcmd = run_command(pkgconfig, '--variable=pluginsdir',
      'gstreamer-' + apiversion, 'gstreamer-plugins-base-' + apiversion,
      'gstreamer-plugins-bad-' + apiversion)

  if runcmd.returncode() == 0
      pluginsdirs = runcmd.stdout().split()
  else
    error('Could not determine GStreamer core plugins directory for unit tests.')
  endif
endif

foreach t : ges_tests
  test_name = t.get(0)
  if t.length() == 2
    skip_test = t.get(1)
  else
    skip_test = false
  endif

  if not skip_test
    env = environment()
    env.set('GST_PLUGIN_PATH_1_0', meson.build_root())
    env.set('GST_PLUGIN_SYSTEM_PATH_1_0', '')
    env.set('GST_STATE_IGNORE_ELEMENTS', '')
    env.set('CK_DEFAULT_TIMEOUT', '20')
    env.set('GST_REGISTRY', '@0@/@1@.registry'.format(meson.current_build_dir(), test_name))
    foreach plugindir: pluginsdirs
      env.append('GST_PLUGIN_PATH_1_0', plugindir)
    endforeach

    exe = executable(test_name, '@0@.c'.format(test_name),
        'ges/test-utils.c', 'nle/common.c',
        c_args : ges_c_args + test_defines,
        include_directories : [configinc],
        dependencies : libges_deps + [gstcheck_dep, ges_dep],
    )
    test(test_name, exe, env: env)
  endif
endforeach

if build_gir
  runtests = find_program('nosetests3', required : false)
  # On some distros it doesn't have the '3' suffix
  if not runtests.found()
      runtests = find_program('nosetests', required : false)
  endif

  if runtests.found()
    testdir = '@0@/python/'.format(meson.current_source_dir())

    tests = [
        ['Test clip', 'test_clip.py'],
        ['Test timeline', 'test_timeline.py'],
        ['Test groups', 'test_group.py']
    ]

    foreach i: tests
      env = environment()
      env.prepend('GI_TYPELIB_PATH', meson.current_build_dir() + '/../../ges/')
      env.prepend('LD_LIBRARY_PATH', meson.current_build_dir() + '/../../ges/')
      args = ['--with-xunit', '--xunit-file=' + meson.current_build_dir() + i.get(0).underscorify() + '.xunit']
      test(i.get(0), runtests, args: args + [testdir + '/' + i.get(1)], env: env)
    endforeach
  endif
endif
