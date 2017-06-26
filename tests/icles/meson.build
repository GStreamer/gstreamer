tests = [
  ['equalizer-test'],
  ['test-accurate-seek', gstapp_dep],
  ['test-segment-seeks'],
  ['videocrop-test'],
  ['videobox-test'],
  ['videocrop2-test'],
]

gtk_dep = dependency('gtk+-3.0', version : '>= 3.0.0', required : false)
if gtk_dep.found()
  tests += [
    ['gdkpixbufsink-test', gtk_dep],
    ['gdkpixbufoverlay-test', [gstvideo_dep, gtk_dep]],
  ]
endif

if cdata.has('HAVE_GST_V4L2')
  tests += [['v4l2src-test', gstvideo_dep]]
endif

if get_variable('have_oss4', false)
  tests += [['test-oss4']]
endif

if get_variable('x11_dep', dependency('', required: false)).found()
  tests += [['ximagesrc-test']]
endif

foreach t : tests
  test_name = t.get(0)
  extra_deps = t.get(1, [])
  executable(test_name, test_name + '.c',
    dependencies: [gst_dep, libm, extra_deps],
    c_args : gst_plugins_good_args,
    include_directories : [configinc],
    install: false)
endforeach
