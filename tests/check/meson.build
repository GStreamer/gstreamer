have_registry = not get_option('disable_registry')
have_debug = true # FIXME
have_parse = true # FIXME

# tests and condition when to skip the test
core_tests = [
  [ 'gst/gst', not have_registry ],
  [ 'gst/gstabi', not have_registry ],
  [ 'gst/gstatomicqueue' ],
  [ 'gst/gstbuffer' ],
  [ 'gst/gstbufferlist' ],
  [ 'gst/gstbufferpool' ],
  [ 'gst/gstchildproxy', not have_registry ],
  [ 'gst/gstclock' ],
  [ 'gst/gstcontext' ],
  [ 'gst/gstcontroller' ],
  [ 'gst/gstcaps' ],
  [ 'gst/gstcapsfeatures' ],
  [ 'gst/gstdatetime' ],
  [ 'gst/gstdevice' ],
  [ 'gst/gstelement', not have_registry ],
  [ 'gst/gstelementfactory', not have_registry ],
  [ 'gst/gstghostpad', not have_registry ],
  [ 'gst/gstinfo' ],
  [ 'gst/gstiterator' ],
  [ 'gst/gstmessage' ],
  [ 'gst/gstmemory' ],
  [ 'gst/gstmeta' ],
  [ 'gst/gstminiobject' ],
  [ 'gst/gstobject' ],
  [ 'gst/gstpad', not have_registry ],
  [ 'gst/gstparamspecs' ],
  [ 'gst/gstprintf', not have_debug ],
  [ 'gst/gstpipeline', not have_registry ],
  [ 'gst/gstpoll' ],
  [ 'gst/gstplugin', not have_registry ],
  [ 'gst/gstpreset' ],
  [ 'gst/gstprotection' ],
  [ 'gst/gstquery', not have_registry ],
  [ 'gst/gstregistry', not have_registry ],
  [ 'gst/gstsegment' ],
  [ 'gst/gststream' ],
  [ 'gst/gststructure' ],
  [ 'gst/gstsystemclock' ],
  [ 'gst/gsttag' ],
  [ 'gst/gsttagsetter' ],
  [ 'gst/gsttask' ],
  [ 'gst/gsttoc' ],
  [ 'gst/gsttocsetter' ],
  [ 'gst/gsttracerrecord' ],
  [ 'gst/gsturi' ],
  [ 'gst/gstutils', not have_registry ],
  [ 'gst/gstvalue' ],
  [ 'generic/states', not have_registry ],
  [ 'libs/adapter' ],
  [ 'libs/baseparse' ],
  [ 'libs/basesrc', not have_registry ],
  [ 'libs/basesink', not have_registry ],
  [ 'libs/bitreader' ],
  [ 'libs/bytereader' ],
  [ 'libs/bytewriter' ],
  [ 'libs/bitreader-noinline' ],
  [ 'libs/bytereader-noinline' ],
  [ 'libs/bytewriter-noinline' ],
  [ 'libs/collectpads', not have_registry ],
  [ 'libs/controller' ],
  [ 'libs/flowcombiner' ],
  [ 'libs/gstharness' ],
  [ 'libs/gstnetclientclock' ],
  [ 'libs/gstnettimeprovider' ],
  [ 'libs/gsttestclock' ],
  [ 'libs/libsabi' ],
  [ 'libs/sparsefile' ],
  [ 'libs/transform1' ],
  [ 'libs/transform2' ],
  [ 'libs/typefindhelper' ],
  [ 'libs/queuearray' ],
  [ 'elements/capsfilter', not have_registry ],
  [ 'elements/concat', not have_registry ],
  [ 'elements/fakesrc', not have_registry ],
  [ 'elements/fdsrc', not have_registry ],
  [ 'elements/filesink', not have_registry ],
  [ 'elements/filesrc', not have_registry ],
  [ 'elements/funnel', not have_registry ],
  [ 'elements/identity', not have_registry ],
  [ 'elements/multiqueue', not have_registry ],
  [ 'elements/selector', not have_registry ],
  [ 'elements/streamiddemux', not have_registry ],
  [ 'elements/tee', not have_registry ],
  [ 'elements/queue', not have_registry ],
  [ 'elements/queue2', not have_registry ],
  [ 'elements/valve', not have_registry ],
  [ 'pipelines/seek', not have_registry ],
  [ 'pipelines/queue-error', not have_registry ],
  [ 'pipelines/parse-disabled', have_parse ],
  [ 'pipelines/simple-launch-lines', not have_parse ],
  [ 'pipelines/parse-launch', not have_parse ],
  [ 'pipelines/cleanup', not have_parse ],
  [ 'tools/gstinspect' ],
  # These take quite long, put them at the end
  [ 'elements/fakesink', not have_registry ],
  [ 'gst/gstbin', not have_registry ],
  [ 'gst/gstbus', not have_registry ],
  [ 'gst/gstevent', not have_registry ],
  [ 'pipelines/stress', not have_registry ],
  [ 'generic/sinks', not have_registry ],
]

test_defines = [
  '-UG_DISABLE_ASSERT',
  '-UG_DISABLE_CAST_CHECKS',
  '-DGST_CHECK_TEST_ENVIRONMENT_BEACON="GST_STATE_IGNORE_ELEMENTS"',
  '-DTESTFILE="' + meson.current_source_dir() + '/meson.build"',
  '-DGST_USE_UNSTABLE_API',
]

test_env = [
  'GST_PLUGIN_SYSTEM_PATH_1_0=',
  'GST_PLUGIN_PATH_1_0=' + meson.build_root() + '/plugins',
  'GST_PLUGIN_SCANNER_1_0='+ meson.build_root() + '/libs/gst/helpers/gst-plugin-scanner',
  'GST_STATE_IGNORE_ELEMENTS=',
  'CK_DEFAULT_TIMEOUT=20',
]

glib_deps = [gio_dep, gobject_dep, gmodule_dep, glib_dep]
gst_deps = [gst_dep, gst_base_dep, gst_check_dep, gst_net_dep, gst_controller_dep]

foreach t : core_tests
  test_name = t.get(0)
  if t.length() == 2
    skip_test = t.get(1)
  else
    skip_test = false
  endif
  if not skip_test
    exe = executable(test_name, '@0@.c'.format(test_name),
      c_args : gst_c_args + test_defines,
      include_directories : [configinc],
      dependencies : glib_deps + gst_deps,
    )
    test(test_name, exe,
      env: test_env + ['GST_REGISTRY=@0@/@1@.registry'.format(meson.current_build_dir(), test_name)],
      timeout : 3 * 60
    )
  endif
endforeach

if have_registry
  test('gstcpp', executable('gstcpp', 'gst/gstcpp.cc',
    cpp_args : gst_c_args + test_defines,
    include_directories : [configinc],
    dependencies : glib_deps + gst_deps),
    env: test_env + ['GST_REGISTRY=@0@/gstcpp.registry'.format(meson.current_build_dir())])
endif

test('gstlibscpp', executable('gstlibscpp', 'libs/gstlibscpp.cc',
    cpp_args : gst_c_args,
    include_directories : [configinc],
    dependencies : glib_deps + gst_deps),
    env: test_env + ['GST_REGISTRY=@0@/gstlibscpp.registry'.format(meson.current_build_dir())])
