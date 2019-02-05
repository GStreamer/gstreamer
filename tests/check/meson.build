have_registry = true # FIXME get_option('registry')

libparser = static_library('parser',
  'elements/parser.c',
  install : false,
  dependencies : [gst_dep, gstcheck_dep],
)

libparser_dep = declare_dependency(link_with: libparser,
  sources: ['elements/parser.h'])

# FIXME: automagic
exif_dep = dependency('libexif', version : '>= 0.6.16', required : false)

enable_gst_player_tests = get_option('gst_player_tests')

# name, condition when to skip the test and extra dependencies
base_tests = [
  [['elements/aiffparse.c']],
  [['elements/asfmux.c']],
  [['elements/autoconvert.c']],
  [['elements/autovideoconvert.c']],
  [['elements/avwait.c']],
  [['elements/camerabin.c']],
  [['elements/gdpdepay.c']],
  [['elements/gdppay.c']],
  [['elements/h263parse.c'], false, [libparser_dep, gstcodecparsers_dep]],
  [['elements/h264parse.c'], false, [libparser_dep, gstcodecparsers_dep]],
  [['elements/id3mux.c']],
  [['elements/mpegtsmux.c'], false, [gstmpegts_dep]],
  [['elements/mpeg4videoparse.c'], false, [libparser_dep, gstcodecparsers_dep]],
  [['elements/mpegvideoparse.c'], false, [libparser_dep, gstcodecparsers_dep]],
  [['elements/msdkh264enc.c'], not have_msdk, [msdk_dep]],
  [['elements/mxfdemux.c']],
  [['elements/mxfmux.c']],
  [['elements/pcapparse.c'], false, [libparser_dep]],
  [['elements/pnm.c']],
  [['elements/rtponvifparse.c']],
  [['elements/rtponviftimestamp.c']],
  [['elements/videoframe-audiolevel.c']],
  [['elements/viewfinderbin.c']],
  [['libs/h264parser.c'], false, [gstcodecparsers_dep]],
  [['libs/h265parser.c'], false, [gstcodecparsers_dep]],
  [['libs/insertbin.c'], false, [gstinsertbin_dep]],
  [['libs/isoff.c'], false, [gstisoff_dep]],
  [['libs/mpegts.c'], false, [gstmpegts_dep]],
  [['libs/mpegvideoparser.c'], false, [gstcodecparsers_dep]],
  [['libs/planaraudioadapter.c'], false, [gstbadaudio_dep]],
  [['libs/player.c'], not enable_gst_player_tests, [gstplayer_dep]],
  [['libs/vc1parser.c'], false, [gstcodecparsers_dep]],
  [['libs/vp8parser.c'], false, [gstcodecparsers_dep]],
]

# FIXME: unistd dependency, unstable or not tested yet on windows
if host_machine.system() != 'windows'
  base_tests += [
    [['elements/assrender.c'], not ass_dep.found(), [ass_dep]],
    [['elements/ccconverter.c']],
    [['elements/cccombiner.c']],
    [['elements/ccextractor.c']],
    [['elements/curlhttpsink.c'], not curl_dep.found(), [curl_dep]],
    [['elements/curlhttpsrc.c'], not curl_dep.found(), [curl_dep, gio_dep]],
    [['elements/curlfilesink.c'],
        not curl_dep.found() or not cdata.has('HAVE_UNISTD_H'), [curl_dep]],
    [['elements/curlftpsink.c'], not curl_dep.found(), [curl_dep]],
    [['elements/curlsmtpsink.c'], not curl_dep.found(), [curl_dep]],
    [['elements/dash_mpd.c'], not xml2_dep.found(), [xml2_dep]],
    [['elements/dtls.c'], not libcrypto_dep.found(), [libcrypto_dep]],
    [['elements/faac.c'],
        not faac_dep.found() or not cc.has_header_symbol('faac.h', 'faacEncOpen') or not cdata.has('HAVE_UNISTD_H'),
        [faac_dep]],
    [['elements/faad.c'],
        not faad_dep.found() or not have_faad_2_7 or not cdata.has('HAVE_UNISTD_H'),
        [faad_dep]],
    [['elements/jifmux.c'],
        not exif_dep.found() or not cdata.has('HAVE_UNISTD_H'), [exif_dep]],
    [['elements/jpegparse.c'], not cdata.has('HAVE_UNISTD_H')],
    [['elements/kate.c'],
        not kate_dep.found() or not cdata.has('HAVE_UNISTD_H'), [kate_dep]],
    [['elements/netsim.c']],
    [['elements/shm.c'], not shm_enabled, shm_deps],
    [['elements/voaacenc.c'],
        not voaac_dep.found() or not cdata.has('HAVE_UNISTD_H'), [voaac_dep]],
    [['elements/webrtcbin.c'], not libnice_dep.found(), [gstwebrtc_dep]],
    [['elements/x265enc.c'], not x265_dep.found(), [x265_dep]],
    [['elements/zbar.c'], not zbar_dep.found(), [zbar_dep]],
  ]
endif

test_defines = [
  '-UG_DISABLE_ASSERT',
  '-UG_DISABLE_CAST_CHECKS',
  '-DGST_CHECK_TEST_ENVIRONMENT_BEACON="GST_STATE_IGNORE_ELEMENTS"',
  '-DGST_TEST_FILES_PATH="' + meson.current_source_dir() + '/../files"',
  '-DTEST_PATH="' + meson.current_build_dir() + '/media"',
  '-DDASH_MPD_DATADIR=' + meson.current_source_dir() + '/elements/dash_mpd_data',
  '-DGST_USE_UNSTABLE_API',
]

test_deps = [gst_dep, gstapp_dep, gstbase_dep,
             gstbasecamerabin_dep, gstphotography_dep,
             gstpbutils_dep, gstcontroller_dep, gstaudio_dep,
             gstvideo_dep, gstrtp_dep, gsturidownloader_dep,
             gstcheck_dep, gio_dep, glib_dep, gsttag_dep]

pluginsdirs = [  ]

if gst_dep.type_name() == 'pkgconfig'
  pbase = dependency('gstreamer-plugins-base-' + api_version)

  pluginsdirs = [gst_dep.get_pkgconfig_variable('pluginsdir')] + [pbase.get_pkgconfig_variable('pluginsdir')]
endif

foreach t : base_tests
  fnames = t.get(0)
  test_name = fnames[0].split('.').get(0).underscorify()
  skip_test = false
  extra_deps = [ ]

  if t.length() >= 3
    extra_deps = t.get(2)
  endif

  if t.length() >= 2
    skip_test = t.get(1)
  endif

  if not skip_test
    exe = executable(test_name, fnames,
      include_directories : [configinc],
      c_args : ['-DHAVE_CONFIG_H=1' ] + test_defines,
      cpp_args : gst_plugins_bad_args,
      dependencies : [libm] + test_deps + extra_deps,
    )

    env = environment()
    env.set('GST_PLUGIN_PATH_1_0', meson.build_root())
    env.set('GST_PLUGIN_SYSTEM_PATH_1_0', '')
    env.set('CK_DEFAULT_TIMEOUT', '20')
    env.set('GST_STATE_IGNORE_ELEMENTS', '')
    env.set('GST_PLUGIN_PATH_1_0', [meson.build_root()] + pluginsdirs)
    env.set('GST_REGISTRY', join_paths(meson.current_build_dir(), '@0@.registry'.format(test_name)))
    env.set('GST_PLUGIN_LOADING_WHITELIST', 'gstreamer', 'gst-plugins-base',
      'gst-plugins-good', 'gst-plugins-ugly','gst-libav',
      'gst-plugins-bad@' + meson.build_root())
    test(test_name, exe, env: env, timeout: 3 * 60)
  endif
endforeach

# orc tests
orc_tests = [
  ['orc_bayer', files('../../gst/bayer/gstbayerorc.orc')],
]

orc_test_dep = dependency('', required : false)
if have_orcc
  # FIXME: there should really just be a separate orc-test-0.4.pc file for this
  if orc_dep.type_name() == 'pkgconfig'
    orc_test_dep = cc.find_library('orc-test-0.4', required : false)
  else
    orc_test_dep = dependency('', fallback: ['orc', 'orc_test_dep'], required: false)
  endif
endif

if have_orcc and orc_test_dep.found()
  foreach t : orc_tests
    tname = t[0]
    torcfile = t[1]
    tcfilename = '@0@.c'.format(tname)
    test_c = custom_target(tcfilename,
      output: tcfilename,
      input: torcfile,
      command: [orcc, '--include', 'stdint.h', '--test', '-o', '@OUTPUT@', '@INPUT@'])
    test_exe = executable(tname, test_c, dependencies: [orc_dep, orc_test_dep])
    test(tname, test_exe)
  endforeach
endif

if enable_gst_player_tests
  subdir ('media')
endif
