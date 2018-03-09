have_registry = true # FIXME not get_option('disable_registry')

libparser = static_library('parser',
  'elements/parser.c',
  install : false,
  dependencies : [gst_dep, gstcheck_dep],
)

libparser_dep = declare_dependency(link_with: libparser,
  sources: ['elements/parser.h'])

exif_dep = dependency('libexif', version : '>= 0.6.16', required : false)

enable_gst_player_tests = get_option('enable_gst_player_tests')

# name, condition when to skip the test and extra dependencies
base_tests = [
  [['elements/aiffparse.c']],
  [['elements/asfmux.c']],
  [['elements/assrender.c'], not ass_dep.found(), [ass_dep]],
  [['elements/autoconvert.c']],
  [['elements/autovideoconvert.c']],
  [['elements/camerabin.c']],
  [['elements/compositor.c']],
  [['elements/curlhttpsink.c'], not curl_dep.found(), [curl_dep]],
  [['elements/curlfilesink.c'], not curl_dep.found(), [curl_dep]],
  [['elements/curlftpsink.c'], not curl_dep.found(), [curl_dep]],
  [['elements/curlsmtpsink.c'], not curl_dep.found(), [curl_dep]],
  [['elements/dash_mpd.c'], not xml2_dep.found(), [xml2_dep]],
  [['elements/dtls.c'], not libcrypto_dep.found(), [libcrypto_dep]],
  [['elements/faac.c'], not faac_dep.found() or not cc.has_header_symbol('faac.h', 'faacEncOpen'), [faac_dep]],
  [['elements/faad.c'], not faad_dep.found() or not have_faad_2_7, [faad_dep]],
  [['elements/gdpdepay.c']],
  [['elements/gdppay.c']],
  [['elements/h263parse.c'], false, [libparser_dep]],
  [['elements/h264parse.c'], false, [libparser_dep]],
  [['elements/id3mux.c']],
  [['elements/jifmux.c'], not exif_dep.found(), [exif_dep]],
  [['elements/jpegparse.c']],
  [['elements/kate.c'], not kate_dep.found(), [kate_dep]],
  [['elements/mpeg4videoparse.c'], false, [libparser_dep]],
  [['elements/mpegtsmux.c']],
  [['elements/mpegvideoparse.c'], false, [libparser_dep]],
  [['elements/mssdemux.c', 'elements/test_http_src.c', 'elements/adaptive_demux_engine.c', 'elements/adaptive_demux_common.c'], not xml28_dep.found(), [xml28_dep]],
  [['elements/mxfdemux.c']],
  [['elements/mxfmux.c']],
  [['elements/netsim.c']],
  [['elements/pcapparse.c'], false, [libparser_dep]],
  [['elements/pnm.c']],
  [['elements/shm.c'], not shm_enabled, shm_deps],
  [['elements/rtponvifparse.c']],
  [['elements/rtponviftimestamp.c']],
  [['elements/videoframe-audiolevel.c']],
  [['elements/viewfinderbin.c']],
  [['elements/voaacenc.c'], not voaac_dep.found(), [voaac_dep]],
  [['elements/webrtcbin.c'], not libnice_dep.found(), [gstwebrtc_dep]],
  [['elements/x265enc.c'], not x265_dep.found(), [x265_dep]],
  [['elements/zbar.c'], not zbar_dep.found(), [zbar_dep]],
  [['elements/msdkh264enc.c'], not have_msdk, [msdk_dep]],
  [['libs/h264parser.c'], false, [gstcodecparsers_dep]],
  [['libs/h265parser.c'], false, [gstcodecparsers_dep]],
  [['libs/insertbin.c'], false, [gstinsertbin_dep]],
  [['libs/isoff.c'], not xml2_dep.found(), [gstisoff_dep, xml2_dep]],
  [['libs/mpegts.c'], false, [gstmpegts_dep]],
  [['libs/mpegvideoparser.c'], false, [gstcodecparsers_dep]],
  [['libs/player.c'], not enable_gst_player_tests, [gstplayer_dep]],
  [['libs/vc1parser.c'], false, [gstcodecparsers_dep]],
  [['libs/vp8parser.c'], false, [gstcodecparsers_dep]],
]

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
    env.set('GST_REGISTRY', '@0@/@1@.registry'.format(meson.current_build_dir(), test_name))
    test(test_name, exe, env: env, timeout: 3 * 60)
  endif
endforeach

if enable_gst_player_tests
  subdir ('media')
endif
