have_registry = true # FIXME not get_option('disable_registry')

# name, condition when to skip the test and extra dependencies
base_tests = [
  [ 'gst/typefindfunctions', not have_registry ],
  [ 'libs/allocators' ],
  [ 'libs/audio' ],
  [ 'libs/audiocdsrc' ],
  [ 'libs/audiodecoder' ],
  [ 'libs/audioencoder' ],
  [ 'libs/baseaudiovisualizer' ],
  [ 'libs/discoverer' ],
  [ 'libs/fft' ],
  [ 'libs/libsabi' ],
  [ 'libs/mikey' ],
  [ 'libs/navigation' ],
  [ 'libs/pbutils' ],
  [ 'libs/profile' ],
  [ 'libs/rtpbasedepayload' ],
  [ 'libs/rtpbasepayload' ],
  [ 'libs/rtp' ],
  [ 'libs/rtsp' ],
  [ 'libs/rtspconnection' ],
  [ 'libs/sdp' ],
  [ 'libs/tag' ],
#  [ 'libs/video' ], # FIXME: times out, perhaps split or spawn conversions into benchmark tool and/or multiple threads
  [ 'libs/videodecoder' ],
  [ 'libs/videoencoder' ],
  [ 'libs/videotimecode' ],
  [ 'libs/xmpwriter' ],
  [ 'elements/adder' ],
  [ 'elements/appsink' ],
  [ 'elements/appsrc' ],
  [ 'elements/audioconvert' ],
  [ 'elements/audiorate' ],
  [ 'elements/audiotestsrc' ],
  [ 'elements/audioresample' ],
  [ 'elements/libvisual', not libvisual_dep.found() ],
  [ 'elements/decodebin' ],
  [ 'elements/encodebin', not theoraenc_dep.found() or not vorbisenc_dep.found() ],
  [ 'elements/multifdsink' ],
  [ 'elements/multisocketsink' ],
  [ 'elements/playbin' ],
  [ 'elements/playbin-complex' ],
  [ 'elements/playsink' ],
  [ 'elements/streamsynchronizer' ],
  [ 'elements/subparse' ],
  [ 'elements/textoverlay', not pango_dep.found() ],
  [ 'elements/videoconvert' ],
  [ 'elements/videorate' ],
  [ 'elements/videoscale' ],
  [ 'elements/videotestsrc' ],
  [ 'elements/volume', false, [ gst_controller_dep ] ],
  [ 'elements/vorbisdec', not vorbis_dep.found(), [ vorbis_dep, vorbisenc_dep ] ],
  [ 'elements/vorbistag', not vorbisenc_dep.found(), [ vorbis_dep, vorbisenc_dep ] ],
  [ 'generic/clock-selection' ],
  [ 'generic/states' ],
  [ 'pipelines/simple-launch-lines' ],
  [ 'pipelines/basetime' ],
  [ 'pipelines/capsfilter-renegotiation' ],
  [ 'pipelines/gio' ],
  [ 'pipelines/oggmux', not ogg_dep.found(), [ ogg_dep, ] ],
  [ 'pipelines/streamsynchronizer' ],
  [ 'pipelines/tcp' ], # FIXME: -DHAVE_GIO_UNIX_2_0=1
  [ 'pipelines/theoraenc', not theoraenc_dep.found(), [ theoraenc_dep ] ],
  [ 'pipelines/vorbisenc', not vorbisenc_dep.found() ],
  [ 'pipelines/vorbisdec', not vorbisenc_dep.found(),],
]

# FIXME: enable orc testing
#orc_tests = [
#  'orc/video',
#  'orc/audio',
#  'orc/adder',
#  'orc/volume',
#  'orc/videotestsrc'
#]

test_defines = [
  '-UG_DISABLE_ASSERT',
  '-UG_DISABLE_CAST_CHECKS',
  '-DGST_CHECK_TEST_ENVIRONMENT_BEACON="GST_PLUGIN_LOADING_WHITELIST"',
  '-DGST_TEST_FILES_PATH="' + meson.source_root() + '/tests/files"',
  '-DGST_TEST_FILE="' + meson.source_root() + '/tests/files/partialframe.mjpeg"', # for libs/discoverer
  '-DGST_USE_UNSTABLE_API',
]

runcmd = run_command('pkg-config', '--variable=pluginsdir', 'gstreamer-1.0')
if runcmd.returncode() == 0
  core_plugins_dir = runcmd.stdout().strip()
  message('Using GStreamer plug-ins in ' + core_plugins_dir)
else
  error('Could not determine GStreamer core plugins directory for unit tests.')
endif

test_env = [
  'GST_PLUGIN_SYSTEM_PATH_1_0=',
  'GST_PLUGIN_PATH_1_0=' + meson.build_root() + '/gst:' + meson.build_root() + '/ext:' + meson.build_root() + '/sys:' + core_plugins_dir,
  'GST_PLUGIN_LOADING_WHITELIST=gstreamer:gst-plugins-base@' + meson.build_root(),
  'GST_TAG_LICENSE_TRANSLATIONS_DICT=' + meson.source_root() + '/gst-libs/gst/tag/license-translations.dict',
  'GST_STATE_IGNORE_ELEMENTS=cdio cdparanoiasrc libvisual_ alsasrc alsasink',
  'CK_DEFAULT_TIMEOUT=20',
]

test_deps = [gst_dep, gst_base_dep, gst_net_dep, gst_check_dep, audio_dep,
  video_dep, pbutils_dep, rtp_dep, rtsp_dep, tag_dep, allocators_dep, app_dep,
  fft_dep, riff_dep, sdp_dep, gio_dep] + glib_deps

foreach t : base_tests
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
      env: test_env + ['GST_REGISTRY=@0@/@1@.registry'.format(meson.current_build_dir(), test_name)]
    )
  endif
endforeach

# ===============> FIXME <==========
test('gstlibscpp', executable('gstlibscpp', 'libs/gstlibscpp.cc',
  include_directories : [configinc],
  cpp_args : gst_plugins_base_args,
  dependencies : test_deps),
  env: test_env + [ 'GST_REGISTRY=@0@/gstlibscpp.registry'.format(meson.current_build_dir()) ])
