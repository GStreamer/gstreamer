# tests and condition when to skip the test
validate_tests = [
  ['validate/padmonitor'],
  ['validate/monitoring'],
  ['validate/reporting'],
  ['validate/overrides'],
  ['validate/scenario'],
  ['validate/utilities'],
  ['validate/expression_parser'],
]

fsmod = import('fs')
test_defines = [
  '-UG_DISABLE_ASSERT',
  '-UG_DISABLE_CAST_CHECKS',
  '-DGST_CHECK_TEST_ENVIRONMENT_BEACON="GST_STATE_IGNORE_ELEMENTS"',
  '-DTESTFILE="' + fsmod.as_posix(meson.current_source_dir()) + '/meson.build"',
  '-DGST_USE_UNSTABLE_API',
]

foreach t : validate_tests
  fname = '@0@.c'.format(t.get(0))
  test_name = t.get(0).underscorify()
  if t.length() == 2
    skip_test = t.get(1)
  else
    skip_test = false
  endif

  if not skip_test
    env = environment()
    env.set('GST_STATE_IGNORE_ELEMENTS', '')
    env.set('GST_PLUGIN_SYSTEM_PATH_1_0', '')
    env.set('GST_PLUGIN_PATH_1_0', [meson.global_build_root()] + pluginsdirs)
    env.set('GST_REGISTRY', '@0@/@1@.registry'.format(meson.current_build_dir(), test_name))
    env.set('GST_PLUGIN_SCANNER_1_0', gst_plugin_scanner_path)

    exe = executable(test_name, fname,
        'validate/test-utils.c',
        c_args : gst_c_args + test_defines,
        include_directories : [inc_dirs],
        dependencies : [validate_dep, gst_check_dep],
        link_with: gstvalidate
    )
    env.set('GST_REGISTRY',
            '@0@/@1@.registry'.format(meson.current_build_dir(), test_name))
    test(test_name, exe, env: env)
  endif
endforeach

