# name, condition when to skip the test and extra dependencies
ugly_tests = [
  [ 'elements/amrnbenc', not amrnb_dep.found() ],
  [ 'elements/mpeg2dec', not mpeg2_dep.found() ],
  [ 'elements/mpg123audiodec', not mpg123_dep.found() ],
  [ 'elements/x264enc', not x264_dep.found() ],
  [ 'elements/xingmux' ],
  [ 'generic/states' ],
  [ 'pipelines/lame', not lame_dep.found() ],
]

test_defines = [
  '-UG_DISABLE_ASSERT',
  '-UG_DISABLE_CAST_CHECKS',
  '-DGST_CHECK_TEST_ENVIRONMENT_BEACON="GST_PLUGIN_LOADING_WHITELIST"',
  '-DGST_TEST_FILES_PATH="' + meson.source_root() + '/tests/files"',
  '-DGST_USE_UNSTABLE_API',
]

getpluginsdir = find_program('getpluginsdir')
runcmd = run_command(getpluginsdir, 'gstreamer', 'gstreamer-1.0')
if runcmd.returncode() == 0
  plugins_dir = runcmd.stdout().strip()
  message('Using GStreamer plug-ins in ' + plugins_dir + ' for unit tests')
else
  error('Could not determine GStreamer plugins directory for unit tests.')
endif

test_env = [
  'GST_PLUGIN_SYSTEM_PATH_1_0=',
  'GST_PLUGIN_PATH_1_0=' + meson.build_root() + '/gst:' + meson.build_root() + '/ext:' + meson.build_root() + '/sys:' + plugins_dir,
  'GST_PLUGIN_LOADING_WHITELIST=gstreamer:gst-plugins-base:gst-plugins-good:gst-plugins-ugly@' + meson.build_root(),
  'CK_DEFAULT_TIMEOUT=20',
]

test_deps = [gst_dep, gstbase_dep, gstcheck_dep, gstaudio_dep, gstapp_dep, gstfft_dep]

libm = cc.find_library('m', required : false)

# FIXME: valgrind_args: add suppressions $(top_srcdir)/common/gst.supp $(srcdir)/gst-plugins-ugly.supp

foreach t : ugly_tests
  test_name = t.get(0)
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
    exe = executable(test_name, '@0@.c'.format(test_name),
      include_directories : [configinc],
      c_args : ['-DHAVE_CONFIG_H=1' ] + test_defines,
      dependencies : [libm] + test_deps + extra_deps,
    )
    test(test_name, exe,
      env: test_env + ['GST_REGISTRY=@0@/@1@.registry'.format(meson.current_build_dir(), test_name)],
      timeout: 3 * 60
    )
  endif
endforeach
