gtk_dep = dependency('gtk+-3.0', version : '>= 3.0.0', required : false)
if gtk_dep.found()
  executable('gdkpixbufsink-test', 'gdkpixbufsink-test.c',
    dependencies: [gst_dep, gtk_dep],
    c_args : gst_plugins_good_args,
    include_directories : [configinc],
    install: false)

  executable('gdkpixbufoverlay-test', 'gdkpixbufoverlay-test.c',
    dependencies: [gstvideo_dep, gst_dep, gtk_dep, libm],
    c_args : gst_plugins_good_args,
    include_directories : [configinc],
    install: false)
endif

if cdata.has('HAVE_GST_V4L2')
  executable('v4l2src-test', 'v4l2src-test.c',
    dependencies: [gstvideo_dep, gst_dep, libm],
    c_args : gst_plugins_good_args,
    include_directories : [configinc],
    install: false)
endif

if get_variable('have_oss4', false)
  executable('test-oss4', 'test-oss4.c',
    dependencies: gst_dep,
    c_args : gst_plugins_good_args,
    include_directories : [configinc],
    install: false)
endif

if get_variable('x11_dep', dependency('', required: false)).found()
  executable('ximagesrc-test', 'ximagesrc-test.c',
    dependencies: [gst_dep, libm],
    c_args : gst_plugins_good_args,
    include_directories : [configinc],
    install: false)
endif


tests = [
  ['equalizer-test'],
  ['test-accurate-seek', gstapp_dep],
  ['test-segment-seeks'],
  ['videocrop-test'],
  ['videobox-test'],
  ['videocrop2-test'],
]

foreach t : tests
  executable(t.get(0), t.get(0) + '.c',
    dependencies: [gst_dep, libm] + [t.get(1, [])],
    c_args : gst_plugins_good_args,
    include_directories : [configinc],
    install: false)
endforeach
