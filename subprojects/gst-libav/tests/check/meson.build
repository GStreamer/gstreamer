# name, condition when to skip the test and extra dependencies
libav_tests = [
  [ 'elements/avaudenc' ],
  [ 'elements/avdec_adpcm' ],
  [ 'elements/avdemux_ape' ],
  [ 'elements/avvidenc' ],
  [ 'generic/libavcodec-locking' ],
  [ 'generic/plugin-test' ]
]

fsmod = import('fs')
test_defines = [
  '-UG_DISABLE_ASSERT',
  '-UG_DISABLE_CAST_CHECKS',
  '-DGST_CHECK_TEST_ENVIRONMENT_BEACON="GST_PLUGIN_LOADING_WHITELIST"',
  '-DGST_TEST_FILES_PATH="' + fsmod.as_posix(meson.current_source_dir()) + '/../files"',
  '-DGST_USE_UNSTABLE_API',
]

pluginsdirs = []
if gst_dep.type_name() == 'pkgconfig'
  pbase = dependency('gstreamer-plugins-base-' + api_version, required: true)
  pluginsdirs = [gst_dep.get_variable('pluginsdir'),
                 pbase.get_variable('pluginsdir')]
  gst_plugin_scanner_dir = gst_dep.get_variable('pluginscannerdir')
else
  gst_plugin_scanner_dir = subproject('gstreamer').get_variable('gst_scanner_dir')
endif
gst_plugin_scanner_path = join_paths(gst_plugin_scanner_dir, 'gst-plugin-scanner')

# FIXME: check, also + PTHREAD_CFLAGS
test_deps = [gst_dep, gstbase_dep, gstcheck_dep, gstaudio_dep,
  gstvideo_dep, gstpbutils_dep]

# FIXME: add valgrind suppression common/gst.supp gst-libav.supp
foreach t : libav_tests
  fname = '@0@.c'.format(t.get(0))
  test_name = t.get(0).underscorify()
  extra_sources = t.get(3, [ ])
  extra_deps = t.get(2, [ ])
  skip_test = t.get(1, false)
  if not skip_test
    env = environment()
    env.set('GST_PLUGIN_SYSTEM_PATH_1_0', '')
    env.set('CK_DEFAULT_TIMEOUT', '20')
    env.set('GST_PLUGIN_LOADING_WHITELIST', 'gstreamer', 'gst-plugins-base',
      'gst-libav@' + meson.project_build_root())
    env.set('GST_PLUGIN_PATH_1_0', [meson.global_build_root()] + pluginsdirs)
    env.set('GSETTINGS_BACKEND', 'memory')

    env.set('GST_REGISTRY', join_paths(meson.current_build_dir(), '@0@.registry'.format(test_name)))
    env.set('GST_PLUGIN_SCANNER_1_0', gst_plugin_scanner_path)
    exe = executable(test_name, fname, extra_sources,
      include_directories : [configinc],
      c_args : ['-DHAVE_CONFIG_H=1' ] + test_defines,
      dependencies : [libm] + test_deps + extra_deps,
    )
    test(test_name, exe, env: env, timeout: 3 * 60)
  endif
endforeach

