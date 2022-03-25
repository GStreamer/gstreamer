# name, condition when to skip the test and extra dependencies
omx_tests = [
  [ 'generic/states' ],
]

test_defines = [
  '-UG_DISABLE_ASSERT',
  '-UG_DISABLE_CAST_CHECKS',
  '-DGST_CHECK_TEST_ENVIRONMENT_BEACON="GST_PLUGIN_LOADING_WHITELIST"',
]

pluginsdirs = []
if gst_dep.type_name() == 'pkgconfig'
  pbase = dependency('gstreamer-plugins-base-' + api_version, required : false)
  pluginsdirs = [gst_dep.get_variable('pluginsdir'),
                 pbase.get_variable('pluginsdir')]
endif

state_ignore_elements=''

# FIXME: check, also + PTHREAD_CFLAGS
test_deps = [gst_dep, gstbase_dep, gstcheck_dep]

# FIXME: add valgrind suppression common/gst.supp gst-plugins-good.supp
foreach t : omx_tests
  fname = '@0@.c'.format(t.get(0))
  test_name = t.get(0).underscorify()
  extra_deps = [ ]
  if t.length() == 3
    extra_deps = t.get(2)
    skip_test = t.get(1)
  elif t.length() == 2
    skip_test = t.get(1)
  else
    skip_test = false
  endif
  if not skip_test
    env = environment()
    env.set('GST_PLUGIN_SYSTEM_PATH_1_0', '')
    env.set('GST_STATE_IGNORE_ELEMENTS', state_ignore_elements)
    env.set('CK_DEFAULT_TIMEOUT', '20')
    env.set('GST_PLUGIN_LOADING_WHITELIST', 'gstreamer', 'gst-plugins-base',
      'gst-plugins-good', 'gst-omx@' + meson.project_build_root(), separator: ':')
    env.set('GST_PLUGIN_PATH_1_0', [meson.global_build_root()] + pluginsdirs)
    env.set('GSETTINGS_BACKEND', 'memory')
    env.set('GST_OMX_CONFIG_DIR', omx_config_dir)

    env.set('GST_REGISTRY', '@0@/@1@.registry'.format(meson.current_build_dir(), test_name))
    exe = executable(test_name, fname,
      include_directories : [configinc],
      c_args : ['-DHAVE_CONFIG_H=1' ] + test_defines,
      dependencies : [libm] + test_deps + extra_deps,
    )
    test(test_name, exe, env: env, timeout: 3 * 60)
  endif
endforeach
