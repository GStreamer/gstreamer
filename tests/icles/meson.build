base_icles = [
  [ 'benchmark-appsink.c', false, [gst_base_dep, app_dep], true ],
  [ 'benchmark-appsrc.c', false, [gst_base_dep, app_dep], true ],
  [ 'audio-trickplay.c', false, [gst_controller_dep] ],
  [ 'playbin-text.c' ],
  [ 'stress-playbin.c' ],
  [ 'test-effect-switch.c' ],
  [ 'test-overlay-blending.c', false, [video_dep, gio_dep] ],
  [ 'test-scale.c' ],
  [ 'test-resample.c' ],
  [ 'test-box.c' ],
  [ 'test-reverseplay.c' ],
  [ 'stress-videooverlay.c', not x11_dep.found(), [x11_dep, video_dep] ],
  [ 'output-selector-test.c', not x11_dep.found(), [x11_dep] ],
  [ 'input-selector-test.c', not x11_dep.found(), [x11_dep] ],
  [ 'test-colorkey.c', not x11_dep.found() or not gtk_x11_dep.found(), [x11_dep, gtk_x11_dep, video_dep]],
  [ 'test-videooverlay.c', not x11_dep.found() or not gtk_x11_dep.found(), [x11_dep, gtk_x11_dep, video_dep]],
  [ 'test-textoverlay.c', not pango_dep.found(), [pango_dep] ],
  [ 'playback/test.c' ],
  [ 'playback/decodetest.c' ],
  [ 'playback/test2.c' ],
  [ 'playback/test3.c' ],
  [ 'playback/test4.c' ],
  [ 'playback/test5.c' ],
  [ 'playback/test6.c' ],
  [ 'playback/test7.c' ],
]

icle_deps = [gst_dep]

foreach icle : base_icles
  fname = icle.get(0)
  icle_name = fname.split('.').get(0).underscorify()
  skip_icle = false
  is_bench = false
  extra_deps = [ ]

  if icle.length() >= 4
    is_bench = icle.get(3)
  endif

  if icle.length() >= 3
    extra_deps = icle.get(2)
  endif

  if icle.length() >= 2
    skip_icle = icle.get(1)
  endif

  if not skip_icle
    exe = executable(icle_name, fname,
      include_directories : [configinc],
      c_args : ['-DHAVE_CONFIG_H=1' ],
      dependencies : icle_deps + extra_deps,
    )
    if is_bench
      benchmark('bench_' + icle_name, exe)
    endif
  endif
endforeach
