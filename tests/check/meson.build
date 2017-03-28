have_registry = true # FIXME not get_option('disable_registry')

# name, condition when to skip the test and extra dependencies
base_tests = [
  [ 'gst/typefindfunctions.c', not have_registry ],
  [ 'libs/allocators.c' ],
  [ 'libs/audio.c' ],
  [ 'libs/audiocdsrc.c' ],
  [ 'libs/audiodecoder.c' ],
  [ 'libs/audioencoder.c' ],
  [ 'libs/baseaudiovisualizer.c' ],
  [ 'libs/discoverer.c' ],
  [ 'libs/fft.c' ],
  [ 'libs/libsabi.c' ],
  [ 'libs/mikey.c' ],
  [ 'libs/navigation.c' ],
  [ 'libs/pbutils.c' ],
  [ 'libs/profile.c' ],
  [ 'libs/rtpbasedepayload.c' ],
  [ 'libs/rtpbasepayload.c' ],
  [ 'libs/rtp.c' ],
  [ 'libs/rtsp.c' ],
  [ 'libs/rtspconnection.c' ],
  [ 'libs/sdp.c' ],
  [ 'libs/tag.c' ],
  #  [ 'libs/video' ], # FIXME: times out, perhaps split or spawn conversions into benchmark tool and/or multiple threads
  [ 'libs/videoencoder.c' ],
  [ 'libs/videotimecode.c' ],
  [ 'libs/xmpwriter.c' ],
  [ 'elements/adder.c' ],
  [ 'elements/appsink.c' ],
  [ 'elements/appsrc.c' ],
  [ 'elements/audioconvert.c' ],
  [ 'elements/audiorate.c' ],
  [ 'elements/audiotestsrc.c' ],
  [ 'elements/audioresample.c' ],
  [ 'elements/libvisual.c', not libvisual_dep.found() ],
  [ 'elements/decodebin.c' ],
  [ 'elements/encodebin.c', not theoraenc_dep.found() or not vorbisenc_dep.found() ],
  [ 'elements/multifdsink.c' ],
  [ 'elements/multisocketsink.c' ],
  [ 'elements/playbin.c' ],
  [ 'elements/playbin-complex.c', not ogg_dep.found() ],
  [ 'elements/playsink.c' ],
  [ 'elements/streamsynchronizer.c' ],
  [ 'elements/subparse.c' ],
  [ 'elements/textoverlay.c', not pango_dep.found() ],
  [ 'elements/videoconvert.c' ],
  [ 'elements/videorate.c' ],
  [ 'elements/videoscale.c' ],
  [ 'elements/videotestsrc.c' ],
  [ 'elements/volume.c', false, [ gst_controller_dep ] ],
  [ 'elements/vorbisdec.c', not vorbis_dep.found(), [ vorbis_dep, vorbisenc_dep ] ],
  [ 'elements/vorbistag.c', not vorbisenc_dep.found(), [ vorbis_dep, vorbisenc_dep ] ],
  [ 'generic/clock-selection.c' ],
  [ 'generic/states.c' ],
  [ 'pipelines/simple-launch-lines.c' ],
  [ 'pipelines/basetime.c' ],
  [ 'pipelines/capsfilter-renegotiation.c' ],
  [ 'pipelines/gio.c' ],
  [ 'pipelines/oggmux.c', not ogg_dep.found(), [ ogg_dep, ] ],
  [ 'pipelines/streamsynchronizer.c' ],
  [ 'pipelines/tcp.c' ], # FIXME: -DHAVE_GIO_UNIX_2_0=1
  [ 'pipelines/theoraenc.c', not theoraenc_dep.found(), [ theoraenc_dep ] ],
  [ 'pipelines/vorbisenc.c', not vorbisenc_dep.found() ],
  [ 'pipelines/vorbisdec.c', not vorbisenc_dep.found(),],
  [ 'libs/gstlibscpp.cc', false, [  ] ],
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
  '-DGST_TEST_FILES_PATH="' + meson.current_source_dir() + '/../files"',
  '-DGST_TEST_FILE="' + meson.current_source_dir() + '/../files/partialframe.mjpeg"', # for libs/discoverer
  '-DGST_USE_UNSTABLE_API',
]

test_deps = [gst_dep, gst_base_dep, gst_net_dep, gst_check_dep, audio_dep,
  video_dep, pbutils_dep, rtp_dep, rtsp_dep, tag_dep, allocators_dep, app_dep,
  fft_dep, riff_dep, sdp_dep, gio_dep] + glib_deps

pluginsdirs = [  ]
if gst_dep.type_name() == 'pkgconfig'
  pluginsdirs = [gst_dep.get_pkgconfig_variable('pluginsdir')]
endif

foreach t : base_tests
  fname = t.get(0)
  test_name = fname.split('.').get(0)
  skip_test = false
  extra_deps = [ ]

  if t.length() >= 3
    extra_deps = t.get(2)
  endif

  if t.length() >= 2
    skip_test = t.get(1)
  endif

  if not skip_test
    exe = executable(test_name, fname,
      include_directories : [configinc],
      c_args : ['-DHAVE_CONFIG_H=1' ] + test_defines,
      cpp_args : gst_plugins_base_args,
      dependencies : [libm] + test_deps + extra_deps,
    )

    env = environment()
    env.set('GST_PLUGIN_PATH_1_0',
        meson.build_root(), '@0@/@1@'.format(get_option('prefix'), plugins_install_dir),
        pluginsdirs)
    env.set('GST_PLUGIN_SYSTEM_PATH_1_0', '')
    env.set('GST_STATE_IGNORE_ELEMENTS', 'cdio cdparanoiasrc libvisual_ alsasrc alsasink')
    env.set('CK_DEFAULT_TIMEOUT', '20')
    env.set('GST_TAG_LICENSE_TRANSLATIONS_DICT', gst_tag_dir + '/license-translations.dict')
    env.set('GST_PLUGIN_LOADING_WHITELIST', 'gstreamer',
        'gst-plugins-base@' + meson.build_root(), separator: ':')
    env.set('GST_REGISTRY', '@0@/@1@.registry'.format(meson.current_build_dir(), test_name))
    test(test_name, exe, env: env, timeout: 3 * 60)
  endif
endforeach

# videoscale tests (split in groups)
foreach group : [1, 2, 3, 4, 5, 6]
  vscale_test_name = 'elements/videoscale-@0@'.format(group)
  exe = executable(vscale_test_name, 'elements/videoscale.c',
      include_directories : [configinc],
      c_args : ['-DHAVE_CONFIG_H=1', '-DVSCALE_TEST_GROUP=@0@'.format(group) ] + test_defines,
      dependencies : [libm] + test_deps + extra_deps)

  # TODO Use env.copy when it is in meson
  env = environment()
  env.set('GST_PLUGIN_PATH_1_0',
      meson.build_root(), '@0@/@1@'.format(get_option('prefix'), plugins_install_dir),
      pluginsdirs)
  env.set('GST_PLUGIN_SYSTEM_PATH_1_0', '')
  env.set('GST_STATE_IGNORE_ELEMENTS', 'cdio cdparanoiasrc libvisual_ alsasrc alsasink')
  env.set('CK_DEFAULT_TIMEOUT', '20')
  env.set('GST_TAG_LICENSE_TRANSLATIONS_DICT', gst_tag_dir + '/license-translations.dict')
  env.set('GST_PLUGIN_LOADING_WHITELIST', 'gstreamer',
      'gst-plugins-base@' + meson.build_root(), separator: ':')
  env.set('GST_REGISTRY', '@0@/@1@.registry'.format(meson.current_build_dir(), test_name))

  test(vscale_test_name, exe, env: env, timeout: 3 * 60)
endforeach
