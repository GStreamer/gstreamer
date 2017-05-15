have_registry = not get_option('disable_registry')
have_debug = true # FIXME
have_parse = true # FIXME

# tests and condition when to skip the test
core_tests = [
  [ 'gst/gst.c', not have_registry ],
  [ 'gst/gstabi.c', not have_registry ],
  [ 'gst/gstatomicqueue.c' ],
  [ 'gst/gstbuffer.c' ],
  [ 'gst/gstbufferlist.c' ],
  [ 'gst/gstbufferpool.c' ],
  [ 'gst/gstchildproxy.c', not have_registry ],
  [ 'gst/gstclock.c' ],
  [ 'gst/gstcontext.c' ],
  [ 'gst/gstcontroller.c' ],
  [ 'gst/gstcaps.c' ],
  [ 'gst/gstcapsfeatures.c' ],
  [ 'gst/gstdatetime.c' ],
  [ 'gst/gstdevice.c' ],
  [ 'gst/gstelement.c', not have_registry ],
  [ 'gst/gstelementfactory.c', not have_registry ],
  [ 'gst/gstghostpad.c', not have_registry ],
  [ 'gst/gstinfo.c' ],
  [ 'gst/gstiterator.c' ],
  [ 'gst/gstmessage.c' ],
  [ 'gst/gstmemory.c' ],
  [ 'gst/gstmeta.c' ],
  [ 'gst/gstminiobject.c' ],
  [ 'gst/gstobject.c' ],
  [ 'gst/gstpad.c', not have_registry ],
  [ 'gst/gstparamspecs.c' ],
  [ 'gst/gstprintf.c', disable_gst_debug, printf_lib],
  [ 'gst/gstpipeline.c', not have_registry ],
  [ 'gst/gstpoll.c' ],
  [ 'gst/gstplugin.c', not have_registry ],
  [ 'gst/gstpreset.c' ],
  [ 'gst/gstprotection.c' ],
  [ 'gst/gstquery.c', not have_registry ],
  [ 'gst/gstregistry.c', not have_registry ],
  [ 'gst/gstsegment.c' ],
  [ 'gst/gststream.c' ],
  [ 'gst/gststructure.c' ],
  [ 'gst/gstsystemclock.c' ],
  [ 'gst/gsttag.c' ],
  [ 'gst/gsttagsetter.c' ],
  [ 'gst/gsttask.c' ],
  [ 'gst/gsttoc.c' ],
  [ 'gst/gsttocsetter.c' ],
  [ 'gst/gsttracerrecord.c' ],
  [ 'gst/gsturi.c' ],
  [ 'gst/gstutils.c', not have_registry ],
  [ 'gst/gstvalue.c' ],
  [ 'generic/states.c', not have_registry ],
  [ 'libs/adapter.c' ],
  [ 'libs/baseparse.c' ],
  [ 'libs/basesrc.c', not have_registry ],
  [ 'libs/basesink.c', not have_registry ],
  [ 'libs/bitreader.c' ],
  [ 'libs/bytereader.c' ],
  [ 'libs/bytewriter.c' ],
  [ 'libs/bitreader-noinline.c' ],
  [ 'libs/bytereader-noinline.c' ],
  [ 'libs/bytewriter-noinline.c' ],
  [ 'libs/collectpads.c', not have_registry ],
  [ 'libs/controller.c' ],
  [ 'libs/flowcombiner.c' ],
  [ 'libs/gstharness.c' ],
  [ 'libs/gstnetclientclock.c' ],
  [ 'libs/gstnettimeprovider.c' ],
  [ 'libs/gsttestclock.c' ],
  [ 'libs/libsabi.c' ],
  [ 'libs/sparsefile.c' ],
  [ 'libs/transform1.c' ],
  [ 'libs/transform2.c' ],
  [ 'libs/typefindhelper.c' ],
  [ 'libs/queuearray.c' ],
  [ 'elements/capsfilter.c', not have_registry ],
  [ 'elements/concat.c', not have_registry ],
  [ 'elements/dataurisrc.c', not have_registry ],
  [ 'elements/fakesrc.c', not have_registry ],
  [ 'elements/fdsrc.c', not have_registry ],
  [ 'elements/filesink.c', not have_registry ],
  [ 'elements/filesrc.c', not have_registry ],
  [ 'elements/funnel.c', not have_registry ],
  [ 'elements/identity.c', not have_registry ],
  [ 'elements/multiqueue.c', not have_registry ],
  [ 'elements/selector.c', not have_registry ],
  [ 'elements/streamiddemux.c', not have_registry ],
  [ 'elements/tee.c', not have_registry ],
  [ 'elements/queue.c', not have_registry ],
  [ 'elements/queue2.c', not have_registry ],
  [ 'elements/valve.c', not have_registry ],
  [ 'pipelines/seek.c', not have_registry ],
  [ 'pipelines/queue-error.c', not have_registry ],
  [ 'pipelines/parse-disabled.c', have_parse ],
  [ 'pipelines/simple-launch-lines.c', not have_parse ],
  [ 'pipelines/parse-launch.c', not have_parse ],
  [ 'pipelines/cleanup.c', not have_parse ],
  [ 'tools/gstinspect.c' ],
  # These take quite long, put them at the end
  [ 'elements/fakesink.c', not have_registry ],
  [ 'gst/gstbin.c', not have_registry ],
  [ 'gst/gstbus.c', not have_registry ],
  [ 'gst/gstevent.c', not have_registry ],
  [ 'pipelines/stress.c', not have_registry ],
  [ 'generic/sinks.c', not have_registry ],
  [ 'gst/gstcpp.cc', not have_registry ],
  [ 'libs/gstlibscpp.cc', false ],
]

test_defines = [
  '-UG_DISABLE_ASSERT',
  '-UG_DISABLE_CAST_CHECKS',
  '-DGST_CHECK_TEST_ENVIRONMENT_BEACON="GST_STATE_IGNORE_ELEMENTS"',
  '-DTESTFILE="' + meson.current_source_dir() + '/meson.build"',
  '-DGST_USE_UNSTABLE_API',
]

glib_deps = [gio_dep, gobject_dep, gmodule_dep, glib_dep]
gst_deps = [gst_dep, gst_base_dep, gst_check_dep, gst_net_dep, gst_controller_dep]

foreach t : core_tests
  fname = t.get(0)
  test_name = fname.split('.').get(0)
  skip_test = t.get(1, false)
  link_with_libs = t.get(2, [])

  if not skip_test
    exe = executable(test_name, fname,
        c_args : gst_c_args + test_defines,
        cpp_args : gst_c_args + test_defines,
        include_directories : [configinc],
        link_with : link_with_libs,
        dependencies : test_deps + glib_deps + gst_deps,
    )

    env = environment()
    env.set('GST_PLUGIN_PATH_1_0', meson.build_root())
    env.set('GST_PLUGIN_SYSTEM_PATH_1_0', '')
    env.set('GST_STATE_IGNORE_ELEMENTS', '')
    env.set('CK_DEFAULT_TIMEOUT', '20')
    env.set('GST_REGISTRY', '@0@/@1@.registry'.format(meson.current_build_dir(), test_name))
    env.set('GST_PLUGIN_SCANNER_1_0', gst_scanner_dir + '/gst-plugin-scanner')
    env.set('GST_PLUGIN_LOADING_WHITELIST', 'gstreamer')

    test(test_name, exe, env: env, timeout : 3 * 60)
  endif
endforeach
